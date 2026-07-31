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
#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// Non-owning C++ context the NSURLSession delegate reads/writes. Lets the data
// task deliver body bytes incrementally to a BodyWriter (streaming) or buffer
// them, and enforce max_response_bytes, without buffering the whole response.
struct MogAppleContext
{
    const mog::BodyWriter *writer = nullptr; // streaming target; null = buffer
    std::string body;                        // buffered body when not streaming
    std::size_t max_bytes = 0;               // 0 = unlimited
    std::size_t received = 0;
    bool had_error = false;
    mog::Error error{mog::ErrorCode::IoError, "native request failed"};
    int status = 0;
    std::vector<mog::Header> headers;
    std::string url;
    int redirects = 0;
};

// Map an NSURLSession NSError to a mog::Error (file scope so the delegate can use it).
static mog::Error MogMapNsError(NSError *err)
{
    const std::string msg = err.localizedDescription.UTF8String != nullptr
                                ? std::string(err.localizedDescription.UTF8String)
                                : "native request failed";
    switch (err.code)
    {
    case NSURLErrorTimedOut:
        return mog::Error{mog::ErrorCode::Timeout, msg};
    case NSURLErrorCannotFindHost:
    case NSURLErrorDNSLookupFailed:
        return mog::Error{mog::ErrorCode::DnsFailed, msg};
    case NSURLErrorCannotConnectToHost:
    case NSURLErrorNetworkConnectionLost:
    case NSURLErrorNotConnectedToInternet:
        return mog::Error{mog::ErrorCode::ConnectFailed, msg};
    case NSURLErrorSecureConnectionFailed:
    case NSURLErrorServerCertificateUntrusted:
    case NSURLErrorServerCertificateHasBadDate:
    case NSURLErrorServerCertificateHasUnknownRoot:
    case NSURLErrorServerCertificateNotYetValid:
    case NSURLErrorClientCertificateRejected:
    case NSURLErrorClientCertificateRequired:
        return mog::Error{mog::ErrorCode::TlsFailed, msg};
    case NSURLErrorHTTPTooManyRedirects:
        return mog::Error{mog::ErrorCode::TooManyRedirects, msg};
    case NSURLErrorBadURL:
    case NSURLErrorUnsupportedURL:
        return mog::Error{mog::ErrorCode::InvalidUrl, msg};
    default:
        return mog::Error{mog::ErrorCode::IoError, msg};
    }
}

@interface MogSessionDelegate : NSObject <NSURLSessionDataDelegate>
@property(nonatomic, assign) BOOL allowRedirects;
@property(nonatomic, assign) BOOL verify;
@property(nonatomic, assign) MogAppleContext *ctx;
@property(nonatomic, strong) id semHolder; // retains the dispatch_semaphore_t
@property(nonatomic, assign) dispatch_semaphore_t sem;
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
        if (self.ctx != nullptr)
        {
            self.ctx->redirects += 1;
        }
        completionHandler(request);
    }
    else
    {
        completionHandler(nil); // stop; the 3xx response is returned as-is
    }
}

- (void)URLSession:(NSURLSession *)session
              dataTask:(NSURLSessionDataTask *)dataTask
    didReceiveResponse:(NSURLResponse *)response
     completionHandler:(void (^)(NSURLSessionResponseDisposition))completionHandler
{
    (void)session;
    (void)dataTask;
    auto *ctx = self.ctx;
    if (ctx != nullptr && [response isKindOfClass:[NSHTTPURLResponse class]])
    {
        auto *http = static_cast<NSHTTPURLResponse *>(response);
        ctx->status = static_cast<int>(http.statusCode);
        if (http.URL != nil)
        {
            ctx->url = std::string(http.URL.absoluteString.UTF8String);
        }
        ctx->headers.clear();
        for (NSString *key in http.allHeaderFields)
        {
            NSString *value = [NSString stringWithFormat:@"%@", http.allHeaderFields[key]];
            ctx->headers.push_back(
                mog::Header{std::string(key.UTF8String), std::string(value.UTF8String)});
        }
    }
    completionHandler(NSURLSessionResponseAllow);
}

- (void)URLSession:(NSURLSession *)session
          dataTask:(NSURLSessionDataTask *)dataTask
    didReceiveData:(NSData *)data
{
    (void)session;
    auto *ctx = self.ctx;
    if (ctx == nullptr)
    {
        return;
    }
    __block bool abort = false;
    [data enumerateByteRangesUsingBlock:^(const void *bytes, NSRange range, BOOL *stop) {
      const std::size_t n = range.length;
      if (ctx->max_bytes > 0 && ctx->received + n > ctx->max_bytes)
      {
          ctx->had_error = true;
          ctx->error = mog::Error{mog::ErrorCode::ResponseTooLarge,
                                  "response exceeds max_response_bytes"};
          abort = true;
          *stop = YES;
          return;
      }
      ctx->received += n;
      if (ctx->writer != nullptr && *ctx->writer)
      {
          auto w = (*ctx->writer)(std::string_view(static_cast<const char *>(bytes), n));
          if (!w)
          {
              ctx->had_error = true;
              ctx->error = w.error();
              abort = true;
              *stop = YES;
              return;
          }
      }
      else
      {
          ctx->body.append(static_cast<const char *>(bytes), n);
      }
    }];
    if (abort)
    {
        [dataTask cancel];
    }
}

- (void)URLSession:(NSURLSession *)session
                    task:(NSURLSessionTask *)task
    didCompleteWithError:(NSError *)error
{
    (void)session;
    (void)task;
    auto *ctx = self.ctx;
    if (ctx != nullptr && error != nil && !ctx->had_error)
    {
        ctx->had_error = true;
        ctx->error = MogMapNsError(error);
    }
    if (self.sem != nullptr)
    {
        dispatch_semaphore_signal(self.sem);
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

    [[nodiscard]] bool AutoPreferred() const noexcept override
    {
        return true;
    }

    // NSURLSession streams and enforces max_response_bytes; it does not implement
    // Digest (later slice) or PEM CA/mTLS (intentional delta — it uses the OS
    // trust store/keychain). Auto falls back to embedded for those.
    [[nodiscard]] bool Supports(const Options &options) const noexcept override
    {
        if (options.auth.kind == Auth::Kind::Digest)
        {
            return false;
        }
        if (options.ca_bundle.has_value() || options.client_cert.has_value())
        {
            return false;
        }
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

            // A data-task delegate delivers body bytes incrementally (streaming to
            // response_writer or buffering), so nothing large sits in memory and the
            // byte cap is enforced as data arrives. All Obj-C -> C++ extraction runs
            // on the delegate queue into `ctx`; the calling thread only reads C++.
            MogAppleContext ctx;
            ctx.max_bytes = options.max_response_bytes;
            ctx.url = url_str;
            if (options.response_writer)
            {
                ctx.writer = &options.response_writer;
            }

            NSURLSessionConfiguration *cfg =
                [NSURLSessionConfiguration ephemeralSessionConfiguration];
            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            MogSessionDelegate *delegate = [[MogSessionDelegate alloc] init];
            delegate.allowRedirects = options.allow_redirects ? YES : NO;
            delegate.verify = options.verify_tls ? YES : NO;
            delegate.ctx = &ctx;
            delegate.semHolder = sem; // keep it alive under ARC
            delegate.sem = sem;
            NSURLSession *session = [NSURLSession sessionWithConfiguration:cfg
                                                                 delegate:delegate
                                                            delegateQueue:nil];

            NSURLSessionDataTask *task = [session dataTaskWithRequest:req];
            [task resume];
            dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
            [session finishTasksAndInvalidate];

            if (ctx.had_error)
            {
                MOG_LOG_WARN("native: request failed: {}", std::string{ctx.error.message()});
                return Result<Response>::Err(std::move(ctx.error));
            }

            Response response;
            response.status_code = ctx.status;
            response.url = !ctx.url.empty() ? ctx.url : url_str;
            response.headers = std::move(ctx.headers);
            if (!options.response_writer)
            {
                response.body = std::move(ctx.body); // delivered to the writer otherwise
            }
            response.downloaded_bytes = ctx.received;
            response.history_len = ctx.redirects;
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
