/**
 * @file curl_backend.cpp
 * @brief libcurl transport loaded at runtime via dlopen (Backend::Curl).
 *
 * libcurl is never build- or link-time required: the handful of stable libcurl
 * ABI constants are defined inline and every entry point is resolved through
 * mog::SharedLibrary. Reuses PrepareRequest so json/form/multipart/auth/cookie
 * encoding matches the embedded backend. Available() is true only when libcurl
 * actually loads; otherwise Auto/explicit selection falls back / fails loud.
 */

#include "http/detail/curl_backend.hpp"

#include "http/detail/prepare.hpp"
#include "http/detail/url.hpp"
#include "mog/dynload.hpp"
#include "mog/log.hpp"
#include "mog/options.hpp"
#include "mog/response.hpp"
#include "mog/util.hpp"

#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace mog::detail
{
namespace
{

// --- Minimal libcurl ABI (stable values from curl/curl.h) --------------------
using CURL = void;
using CURLcode = int;

constexpr CURLcode CURLE_OK = 0;
constexpr long CURL_GLOBAL_DEFAULT = 3;

// setopt option ids (type prefix: <10000 long, 10000+ pointer, 20000+ funcptr).
constexpr int CURLOPT_URL = 10002;
constexpr int CURLOPT_WRITEDATA = 10001;
constexpr int CURLOPT_WRITEFUNCTION = 20011;
constexpr int CURLOPT_HEADERDATA = 10029;
constexpr int CURLOPT_HEADERFUNCTION = 20079;
constexpr int CURLOPT_HTTPHEADER = 10023;
constexpr int CURLOPT_CUSTOMREQUEST = 10036;
constexpr int CURLOPT_POSTFIELDS = 10015;
constexpr int CURLOPT_POSTFIELDSIZE = 60;
constexpr int CURLOPT_NOBODY = 44;
constexpr int CURLOPT_FOLLOWLOCATION = 52;
constexpr int CURLOPT_MAXREDIRS = 68;
constexpr int CURLOPT_TIMEOUT_MS = 155;
constexpr int CURLOPT_CONNECTTIMEOUT_MS = 156;
constexpr int CURLOPT_SSL_VERIFYPEER = 64;
constexpr int CURLOPT_SSL_VERIFYHOST = 81;
constexpr int CURLOPT_CAINFO = 10065;
constexpr int CURLOPT_SSLCERT = 10025;
constexpr int CURLOPT_SSLKEY = 10087;
constexpr int CURLOPT_KEYPASSWD = 10026;
constexpr int CURLOPT_PROXY = 10004;
constexpr int CURLOPT_ACCEPT_ENCODING = 10102;
constexpr int CURLOPT_NOSIGNAL = 99;
constexpr int CURLOPT_HTTPAUTH = 107;
constexpr int CURLOPT_USERPWD = 10005;
constexpr long CURLAUTH_DIGEST = 2; // (1 << 1)

// getinfo ids (CURLINFO_STRING=0x100000, CURLINFO_LONG=0x200000).
constexpr int CURLINFO_EFFECTIVE_URL = 0x100000 + 1;
constexpr int CURLINFO_RESPONSE_CODE = 0x200000 + 2;
constexpr int CURLINFO_REDIRECT_COUNT = 0x200000 + 20;

// curl_easy_perform error codes we map (others -> IoError).
constexpr CURLcode CURLE_URL_MALFORMAT = 3;
constexpr CURLcode CURLE_COULDNT_RESOLVE_HOST = 6;
constexpr CURLcode CURLE_COULDNT_CONNECT = 7;
constexpr CURLcode CURLE_OPERATION_TIMEDOUT = 28;
constexpr CURLcode CURLE_SSL_CONNECT_ERROR = 35;
constexpr CURLcode CURLE_TOO_MANY_REDIRECTS = 47;
constexpr CURLcode CURLE_PEER_FAILED_VERIFICATION = 60;
constexpr CURLcode CURLE_SSL_CACERT_BADFILE = 77;
constexpr CURLcode CURLE_SSL_CERTPROBLEM = 58;
constexpr CURLcode CURLE_SSL_CIPHER = 59;

using InitFn = CURL *(*)();
using SetoptFn = CURLcode (*)(CURL *, int, ...);
using PerformFn = CURLcode (*)(CURL *);
using GetinfoFn = CURLcode (*)(CURL *, int, ...);
using CleanupFn = void (*)(CURL *);
using SlistAppendFn = void *(*)(void *, const char *);
using SlistFreeFn = void (*)(void *);
using StrerrorFn = const char *(*)(CURLcode);
using GlobalInitFn = CURLcode (*)(long);

struct CurlApi
{
    SharedLibrary lib;
    InitFn easy_init = nullptr;
    SetoptFn easy_setopt = nullptr;
    PerformFn easy_perform = nullptr;
    GetinfoFn easy_getinfo = nullptr;
    CleanupFn easy_cleanup = nullptr;
    SlistAppendFn slist_append = nullptr;
    SlistFreeFn slist_free_all = nullptr;
    StrerrorFn easy_strerror = nullptr;
    GlobalInitFn global_init = nullptr;
    bool ok = false;
};

const CurlApi &LoadCurl()
{
    static CurlApi api = [] {
        CurlApi a;
        static const std::array<const char *, 4> kNames = {"libcurl.so.4", "libcurl.so",
                                                           "libcurl.4.dylib", "libcurl.dylib"};
        for (const char *name : kNames)
        {
            if (a.lib.Open(name))
            {
                break;
            }
        }
        if (!a.lib.is_open())
        {
            MOG_LOG_DEBUG("curl: libcurl not found (tried libcurl.so.4/.so/.dylib)");
            return a;
        }

        auto init = a.lib.Symbol<InitFn>("curl_easy_init");
        auto setopt = a.lib.Symbol<SetoptFn>("curl_easy_setopt");
        auto perform = a.lib.Symbol<PerformFn>("curl_easy_perform");
        auto getinfo = a.lib.Symbol<GetinfoFn>("curl_easy_getinfo");
        auto cleanup = a.lib.Symbol<CleanupFn>("curl_easy_cleanup");
        auto sl_append = a.lib.Symbol<SlistAppendFn>("curl_slist_append");
        auto sl_free = a.lib.Symbol<SlistFreeFn>("curl_slist_free_all");
        auto strerror = a.lib.Symbol<StrerrorFn>("curl_easy_strerror");
        auto global = a.lib.Symbol<GlobalInitFn>("curl_global_init");
        if (!init || !setopt || !perform || !getinfo || !cleanup || !sl_append || !sl_free ||
            !strerror || !global)
        {
            MOG_LOG_WARN("curl: libcurl opened but a required symbol is missing");
            a.lib.Close();
            return a;
        }
        a.easy_init = *init;
        a.easy_setopt = *setopt;
        a.easy_perform = *perform;
        a.easy_getinfo = *getinfo;
        a.easy_cleanup = *cleanup;
        a.slist_append = *sl_append;
        a.slist_free_all = *sl_free;
        a.easy_strerror = *strerror;
        a.global_init = *global;
        a.global_init(CURL_GLOBAL_DEFAULT);
        a.ok = true;
        MOG_LOG_DEBUG("curl: libcurl loaded from {}", std::string{a.lib.path()});
        return a;
    }();
    return api;
}

// Body sink for curl: either streams to a BodyWriter or buffers into a string,
// enforcing max_response_bytes. Returning a short count aborts the transfer.
struct WriteContext
{
    const BodyWriter *writer = nullptr; // streaming target (points at options.response_writer)
    std::string *buffer = nullptr;      // buffered target when writer is null
    std::size_t max_bytes = 0;          // 0 = unlimited
    std::size_t received = 0;
    bool aborted = false;
    Error error{ErrorCode::IoError, "curl write aborted"};
};

extern "C" std::size_t CurlWriteBody(char *ptr, std::size_t size, std::size_t nmemb, void *userdata)
{
    auto *ctx = static_cast<WriteContext *>(userdata);
    const std::size_t n = size * nmemb;
    if (ctx->max_bytes > 0 && ctx->received + n > ctx->max_bytes)
    {
        ctx->aborted = true;
        ctx->error = Error{ErrorCode::ResponseTooLarge, "response exceeds max_response_bytes"};
        return 0; // signals CURLE_WRITE_ERROR
    }
    ctx->received += n;
    if (ctx->writer != nullptr && *ctx->writer)
    {
        auto w = (*ctx->writer)(std::string_view(ptr, n));
        if (!w)
        {
            ctx->aborted = true;
            ctx->error = w.error();
            return 0;
        }
    }
    else if (ctx->buffer != nullptr)
    {
        ctx->buffer->append(ptr, n);
    }
    return n;
}

extern "C" std::size_t CurlWriteHeader(char *buffer, std::size_t size, std::size_t nitems,
                                       void *userdata)
{
    const std::size_t n = size * nitems;
    auto *headers = static_cast<std::vector<Header> *>(userdata);
    std::string line(buffer, n);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
    {
        line.pop_back();
    }
    if (line.rfind("HTTP/", 0) == 0)
    {
        headers->clear(); // status line of a (possibly redirected) response: keep only the last
        return n;
    }
    if (line.empty())
    {
        return n;
    }
    const auto colon = line.find(':');
    if (colon != std::string::npos)
    {
        std::string name = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        {
            value.erase(value.begin());
        }
        headers->push_back(Header{std::move(name), std::move(value)});
    }
    return n;
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

bool IsAcceptEncoding(const std::string &name)
{
    static const std::string kAccept = "accept-encoding";
    if (name.size() != kAccept.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < name.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(name[i])) != kAccept[i])
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

Error MapCurlError(const CurlApi &api, CURLcode code)
{
    const char *msg = api.easy_strerror(code);
    const std::string text = msg != nullptr ? std::string(msg) : "curl request failed";
    switch (code)
    {
    case CURLE_OPERATION_TIMEDOUT:
        return Error{ErrorCode::Timeout, text};
    case CURLE_COULDNT_RESOLVE_HOST:
        return Error{ErrorCode::DnsFailed, text};
    case CURLE_COULDNT_CONNECT:
        return Error{ErrorCode::ConnectFailed, text};
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_PEER_FAILED_VERIFICATION:
    case CURLE_SSL_CERTPROBLEM:
    case CURLE_SSL_CIPHER:
    case CURLE_SSL_CACERT_BADFILE:
        return Error{ErrorCode::TlsFailed, text};
    case CURLE_TOO_MANY_REDIRECTS:
        return Error{ErrorCode::TooManyRedirects, text};
    case CURLE_URL_MALFORMAT:
        return Error{ErrorCode::InvalidUrl, text};
    default:
        return Error{ErrorCode::IoError, text};
    }
}

class CurlTransport final : public Transport
{
  public:
    [[nodiscard]] std::string_view Name() const noexcept override
    {
        return "curl";
    }

    [[nodiscard]] bool Available() const noexcept override
    {
        return LoadCurl().ok;
    }

    [[nodiscard]] bool AutoPreferred() const noexcept override
    {
        return true;
    }

    // The curl backend wires CA bundle and client certs, but not streaming or the
    // Digest challenge/retry; Auto falls back to embedded for those.
    // The curl backend is at parity with embedded (streaming, max_response_bytes,
    // Digest, CA bundle, client certs), so Supports() uses the default (true).

    [[nodiscard]] Result<Response> Execute(Method method, std::string_view url,
                                           const Options &options) override
    {
        const CurlApi &api = LoadCurl();
        if (!api.ok)
        {
            return Result<Response>::Err(
                Error{ErrorCode::UnsupportedBackend, "libcurl is not available (not installed)"});
        }

        const PreparedRequest prepared = PrepareRequest(options);
        const std::string full_url = AppendQuery(url, options.params);
        const auto started = std::chrono::steady_clock::now();

        CURL *handle = api.easy_init();
        if (handle == nullptr)
        {
            return Result<Response>::Err(Error{ErrorCode::Internal, "curl_easy_init failed"});
        }

        std::string body_out;
        std::vector<Header> header_out;
        std::string method_str{ToString(method)};

        WriteContext wctx;
        wctx.max_bytes = options.max_response_bytes;
        const bool streaming = static_cast<bool>(options.response_writer);
        if (streaming)
        {
            wctx.writer = &options.response_writer;
        }
        else
        {
            wctx.buffer = &body_out;
        }

        api.easy_setopt(handle, CURLOPT_URL, full_url.c_str());
        api.easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
        api.easy_setopt(handle, CURLOPT_WRITEFUNCTION, &CurlWriteBody);
        api.easy_setopt(handle, CURLOPT_WRITEDATA, static_cast<void *>(&wctx));
        api.easy_setopt(handle, CURLOPT_HEADERFUNCTION, &CurlWriteHeader);
        api.easy_setopt(handle, CURLOPT_HEADERDATA, static_cast<void *>(&header_out));
        api.easy_setopt(handle, CURLOPT_CUSTOMREQUEST, method_str.c_str());
        if (method == Method::Head)
        {
            api.easy_setopt(handle, CURLOPT_NOBODY, 1L);
        }

        if (!prepared.body.empty())
        {
            api.easy_setopt(handle, CURLOPT_POSTFIELDSIZE, static_cast<long>(prepared.body.size()));
            api.easy_setopt(handle, CURLOPT_POSTFIELDS, prepared.body.data());
        }

        // Let curl advertise and transparently decode content-encoding when we
        // decompress; strip our own Accept-Encoding so the two don't fight.
        void *slist = nullptr;
        for (const auto &h : prepared.headers)
        {
            if (options.decompress && IsAcceptEncoding(h.first))
            {
                continue;
            }
            const std::string line = h.first + ": " + h.second;
            slist = api.slist_append(slist, line.c_str());
        }
        if (slist != nullptr)
        {
            api.easy_setopt(handle, CURLOPT_HTTPHEADER, slist);
        }
        if (options.decompress)
        {
            api.easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");
        }

        if (options.allow_redirects)
        {
            api.easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
            api.easy_setopt(handle, CURLOPT_MAXREDIRS, static_cast<long>(options.max_redirects));
        }

        api.easy_setopt(handle, CURLOPT_TIMEOUT_MS, static_cast<long>(IoTimeout(options).count()));
        api.easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS,
                        static_cast<long>(ConnectTimeout(options).count()));

        api.easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, options.verify_tls ? 1L : 0L);
        api.easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, options.verify_tls ? 2L : 0L);
        if (options.ca_bundle.has_value())
        {
            api.easy_setopt(handle, CURLOPT_CAINFO, options.ca_bundle->c_str());
        }
        if (options.client_cert.has_value())
        {
            api.easy_setopt(handle, CURLOPT_SSLCERT, options.client_cert->c_str());
            const std::string &key = options.client_key.value_or(*options.client_cert);
            api.easy_setopt(handle, CURLOPT_SSLKEY, key.c_str());
            if (!options.client_key_password.empty())
            {
                api.easy_setopt(handle, CURLOPT_KEYPASSWD, options.client_key_password.c_str());
            }
        }
        if (options.proxy.has_value() && !options.proxy->empty())
        {
            api.easy_setopt(handle, CURLOPT_PROXY, options.proxy->c_str());
        }
        std::string userpwd;
        if (options.auth.kind == Auth::Kind::Digest)
        {
            // libcurl performs the 401 challenge/response internally.
            api.easy_setopt(handle, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
            userpwd = options.auth.username + ":" + options.auth.password;
            api.easy_setopt(handle, CURLOPT_USERPWD, userpwd.c_str());
        }

        const CURLcode rc = api.easy_perform(handle);
        if (rc != CURLE_OK)
        {
            // A write-callback abort (size cap or writer error) surfaces as a curl
            // write error; prefer our specific reason.
            Error err = wctx.aborted ? wctx.error : MapCurlError(api, rc);
            if (slist != nullptr)
            {
                api.slist_free_all(slist);
            }
            api.easy_cleanup(handle);
            MOG_LOG_WARN("curl: request failed: {}", std::string{err.message()});
            return Result<Response>::Err(std::move(err));
        }

        long status = 0;
        api.easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
        char *effective_url = nullptr;
        api.easy_getinfo(handle, CURLINFO_EFFECTIVE_URL, &effective_url);
        long redirects = 0;
        api.easy_getinfo(handle, CURLINFO_REDIRECT_COUNT, &redirects);

        if (slist != nullptr)
        {
            api.slist_free_all(slist);
        }
        api.easy_cleanup(handle);

        Response response;
        response.status_code = static_cast<int>(status);
        response.url = effective_url != nullptr ? std::string(effective_url) : full_url;
        response.headers = std::move(header_out);
        if (!streaming)
        {
            response.body = std::move(body_out); // body already delivered to the writer otherwise
        }
        response.downloaded_bytes = wctx.received;
        response.history_len = static_cast<int>(redirects);
        response.backend = "curl";
        response.cookies = CollectCookies(response.headers);
        response.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
        return Result<Response>::Ok(std::move(response));
    }
};

} // namespace

std::unique_ptr<Transport> MakeCurlTransport()
{
    return std::make_unique<CurlTransport>();
}

} // namespace mog::detail
