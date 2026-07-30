/**
 * @file native_backend_apple.mm
 * @brief macOS native backend over NSURLSession (Backend::Native).
 *
 * Reuses PrepareRequest so json/form/multipart/auth/cookie encoding matches the
 * embedded backend. NSURLSession handles TLS, redirects, and transparent gzip.
 *
 * This backend is Available (explicitly selectable via Backend::Native /
 * MOG_BACKEND=native / --backend native, and covered by the cross-backend
 * conformance suite) but is NOT yet Auto-preferred: several embedded features
 * (streaming response_writer, keep-alive pooling semantics, max_response_bytes
 * enforcement, Digest, mTLS/custom CA) are not yet wired here, so Auto keeps
 * using embedded until this reaches parity.
 *
 * Known deltas vs embedded (documented): multiple same-name response headers
 * (e.g. several Set-Cookie) are comma-folded by NSURLSession's allHeaderFields;
 * client-certificate (mTLS) and custom CA bundles are not honored here.
 */

#import <Foundation/Foundation.h>

#include "http/detail/native_backend.hpp"
#include "http/detail/prepare.hpp"
#include "http/detail/transport.hpp"
#include "http/detail/url.hpp"
#include "mog/log.hpp"
#include "mog/options.hpp"
#include "mog/response.hpp"
#include "mog/util.hpp"

#include <cctype>
#include <chrono>
#include <map>
#include <string>
#include <vector>

@interface MogSessionDelegate : NSObject <NSURLSessionDataDelegate>
@property(nonatomic, assign) BOOL allowRedirects;
@property(nonatomic, assign) BOOL verify;
@property(nonatomic, assign) int redirectCount;
@end

@implementation MogSessionDelegate

- (void)URLSession:(NSURLSession *)session
                          task:(NSURLSessionTask *)task
    willPerformHTTPRedirection:(NSHTTPURLResponse *)response
                    newRequest:(NSURLRequest *)request
             completionHandler:(void (^)(NSURLRequest *))completionHandler
{
    (void)session;
    (void)task;
    (void)response;
    if (self.allowRedirects)
    {
        self.redirectCount += 1;
        completionHandler(request);
    }
    else
    {
        completionHandler(nil); // stop; the 3xx response is returned as-is
    }
}

- (void)URLSession:(NSURLSession *)session
    didReceiveChallenge:(NSURLAuthenticationChallenge *)challenge
      completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition, NSURLCredential *))handler
{
    (void)session;
    if (!self.verify &&
        [challenge.protectionSpace.authenticationMethod isEqualToString:NSURLAuthenticationMethodServerTrust])
    {
        NSURLCredential *cred =
            [NSURLCredential credentialForTrust:challenge.protectionSpace.serverTrust];
        handler(NSURLSessionAuthChallengeUseCredential, cred);
        return;
    }
    handler(NSURLSessionAuthChallengePerformDefaultHandling, nil);
}

@end

namespace mog::detail
{
namespace
{

NSString *ToNSString(const std::string &s)
{
    NSString *out = [NSString stringWithUTF8String:s.c_str()];
    return out != nil ? out : @"";
}

bool IsSetCookie(const std::string &name)
{
    static const std::string kSetCookie = "set-cookie";
    if (name.size() != kSetCookie.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < name.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(name[i])) != kSetCookie[i])
        {
            return false;
        }
    }
    return true;
}

std::map<std::string, std::string> CollectCookies(const std::vector<Header> &headers)
{
    std::map<std::string, std::string> cookies;
    for (const auto &h : headers)
    {
        if (!IsSetCookie(h.name))
        {
            continue;
        }
        std::string name;
        std::string value;
        if (ParseSetCookie(h.value, name, value))
        {
            cookies[std::move(name)] = std::move(value);
        }
    }
    return cookies;
}

Error MapError(NSError *err)
{
    const std::string msg = err.localizedDescription.UTF8String != nullptr
                                ? std::string(err.localizedDescription.UTF8String)
                                : "native request failed";
    switch (err.code)
    {
    case NSURLErrorTimedOut:
        return Error{ErrorCode::Timeout, msg};
    case NSURLErrorCannotFindHost:
    case NSURLErrorDNSLookupFailed:
        return Error{ErrorCode::DnsFailed, msg};
    case NSURLErrorCannotConnectToHost:
    case NSURLErrorNetworkConnectionLost:
    case NSURLErrorNotConnectedToInternet:
        return Error{ErrorCode::ConnectFailed, msg};
    case NSURLErrorSecureConnectionFailed:
    case NSURLErrorServerCertificateUntrusted:
    case NSURLErrorServerCertificateHasBadDate:
    case NSURLErrorServerCertificateHasUnknownRoot:
    case NSURLErrorServerCertificateNotYetValid:
    case NSURLErrorClientCertificateRejected:
    case NSURLErrorClientCertificateRequired:
        return Error{ErrorCode::TlsFailed, msg};
    case NSURLErrorHTTPTooManyRedirects:
        return Error{ErrorCode::TooManyRedirects, msg};
    case NSURLErrorBadURL:
    case NSURLErrorUnsupportedURL:
        return Error{ErrorCode::InvalidUrl, msg};
    default:
        return Error{ErrorCode::IoError, msg};
    }
}

class AppleNativeTransport final : public Transport
{
  public:
    [[nodiscard]] std::string_view Name() const noexcept override
    {
        return "native";
    }

    [[nodiscard]] bool Available() const noexcept override
    {
        return true;
    }

    [[nodiscard]] Result<Response> Execute(Method method, std::string_view url,
                                           const Options &options) override
    {
        @autoreleasepool
        {
            const auto started = std::chrono::steady_clock::now();
            const PreparedRequest prepared = PrepareRequest(options);
            const std::string url_str = AppendQuery(url, options.params);

            NSURL *nsurl = [NSURL URLWithString:ToNSString(url_str)];
            if (nsurl == nil)
            {
                return Result<Response>::Err(
                    Error{ErrorCode::InvalidUrl, "invalid url: " + url_str});
            }

            NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:nsurl];
            req.HTTPMethod = ToNSString(std::string{ToString(method)});
            for (const auto &h : prepared.headers)
            {
                [req setValue:ToNSString(h.second) forHTTPHeaderField:ToNSString(h.first)];
            }
            if (!prepared.body.empty())
            {
                req.HTTPBody = [NSData dataWithBytes:prepared.body.data()
                                             length:prepared.body.size()];
            }
            req.timeoutInterval =
                static_cast<double>(IoTimeout(options).count()) / 1000.0;

            NSURLSessionConfiguration *cfg =
                [NSURLSessionConfiguration ephemeralSessionConfiguration];
            MogSessionDelegate *delegate = [[MogSessionDelegate alloc] init];
            delegate.allowRedirects = options.allow_redirects ? YES : NO;
            delegate.verify = options.verify_tls ? YES : NO;
            delegate.redirectCount = 0;
            NSURLSession *session = [NSURLSession sessionWithConfiguration:cfg
                                                                 delegate:delegate
                                                            delegateQueue:nil];

            // Extract everything into C++ inside the completion block, while the
            // Obj-C objects are alive and owned by the block. The calling thread
            // must not message any Obj-C object after the block completes (the
            // task/block, and its captured objects, are released on invalidate).
            __block bool had_error = false;
            __block Error error{ErrorCode::IoError, "native request failed"};
            __block Response response;
            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            NSURLSessionDataTask *task =
                [session dataTaskWithRequest:req
                           completionHandler:^(NSData *d, NSURLResponse *r, NSError *e) {
                             if (e != nil)
                             {
                                 had_error = true;
                                 error = MapError(e);
                                 dispatch_semaphore_signal(sem);
                                 return;
                             }
                             auto *http = static_cast<NSHTTPURLResponse *>(r);
                             response.status_code = static_cast<int>(http.statusCode);
                             response.url = http.URL != nil
                                                ? std::string(http.URL.absoluteString.UTF8String)
                                                : url_str;
                             for (NSString *key in http.allHeaderFields)
                             {
                                 NSString *value =
                                     [NSString stringWithFormat:@"%@", http.allHeaderFields[key]];
                                 response.headers.push_back(Header{std::string(key.UTF8String),
                                                                   std::string(value.UTF8String)});
                             }
                             if (d != nil && d.length > 0)
                             {
                                 response.body.assign(static_cast<const char *>(d.bytes), d.length);
                             }
                             dispatch_semaphore_signal(sem);
                           }];
            [task resume];
            dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
            const int redirects = delegate.redirectCount; // delegate still retained by session
            [session finishTasksAndInvalidate];

            if (had_error)
            {
                MOG_LOG_WARN("native: request failed: {}", std::string{error.message()});
                return Result<Response>::Err(std::move(error));
            }

            response.downloaded_bytes = response.body.size();
            response.history_len = redirects;
            response.backend = "native";
            response.cookies = CollectCookies(response.headers);
            response.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started);
            return Result<Response>::Ok(std::move(response));
        }
    }
};

} // namespace

std::unique_ptr<Transport> MakeNativeTransport()
{
    return std::make_unique<AppleNativeTransport>();
}

} // namespace mog::detail
