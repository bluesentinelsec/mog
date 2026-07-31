/**
 * @file mog_c.cpp
 * @brief Implementation of the C API declared in <mog/mog_c.h>.
 *
 * This is a thin translation layer over the C++ library. It owns two opaque
 * types: mog_request (a Method + URL + Options) and mog_response (a completed
 * mog::Response or a captured mog::Error). Accessors return pointers into the
 * owned objects so callers borrow, never free, individual strings.
 *
 * The boundary is exception-safe: every entry point that allocates or performs
 * work is wrapped so no C++ exception can escape into C.
 */
#include "mog/mog_c.h"

#include "mog/backend.hpp"
#include "mog/error.hpp"
#include "mog/http.hpp"
#include "mog/options.hpp"
#include "mog/response.hpp"
#include "mog/version.hpp"

#include <cctype>
#include <chrono>
#include <new>
#include <string>

// ---------------------------------------------------------------------------
// Opaque handles
// ---------------------------------------------------------------------------

struct mog_request
{
    mog::Method method = mog::Method::Get;
    std::string url;
    mog::Options options;
};

struct mog_response
{
    bool ok = false;
    mog::ErrorCode code = mog::ErrorCode::Ok;
    std::string error_message; ///< Populated only when !ok.
    mog::Response resp;        ///< Populated only when ok.
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

// Stable "" so accessors never return NULL.
const char *kEmpty = "";

// Map the C++ error code to the C enum. A switch (rather than a cast) keeps the
// two enums honest if either list changes.
mog_error_code ToCError(mog::ErrorCode code) noexcept
{
    switch (code)
    {
    case mog::ErrorCode::Ok:                 return MOG_OK;
    case mog::ErrorCode::InvalidUrl:         return MOG_ERR_INVALID_URL;
    case mog::ErrorCode::InvalidArgument:    return MOG_ERR_INVALID_ARGUMENT;
    case mog::ErrorCode::UnsupportedScheme:  return MOG_ERR_UNSUPPORTED_SCHEME;
    case mog::ErrorCode::UnsupportedBackend: return MOG_ERR_UNSUPPORTED_BACKEND;
    case mog::ErrorCode::DnsFailed:          return MOG_ERR_DNS_FAILED;
    case mog::ErrorCode::ConnectFailed:      return MOG_ERR_CONNECT_FAILED;
    case mog::ErrorCode::TlsFailed:          return MOG_ERR_TLS_FAILED;
    case mog::ErrorCode::Timeout:            return MOG_ERR_TIMEOUT;
    case mog::ErrorCode::IoError:            return MOG_ERR_IO;
    case mog::ErrorCode::ProtocolError:      return MOG_ERR_PROTOCOL;
    case mog::ErrorCode::TooManyRedirects:   return MOG_ERR_TOO_MANY_REDIRECTS;
    case mog::ErrorCode::HttpStatus:         return MOG_ERR_HTTP_STATUS;
    case mog::ErrorCode::ResponseTooLarge:   return MOG_ERR_RESPONSE_TOO_LARGE;
    case mog::ErrorCode::ProxyError:         return MOG_ERR_PROXY;
    case mog::ErrorCode::FileError:          return MOG_ERR_FILE;
    case mog::ErrorCode::JsonError:          return MOG_ERR_JSON;
    case mog::ErrorCode::CompressionError:   return MOG_ERR_COMPRESSION;
    case mog::ErrorCode::DynamicLibraryError:return MOG_ERR_DYNAMIC_LIBRARY;
    case mog::ErrorCode::Internal:           return MOG_ERR_INTERNAL;
    }
    return MOG_ERR_INTERNAL;
}

mog::ErrorCode FromCError(mog_error_code code) noexcept
{
    switch (code)
    {
    case MOG_OK:                     return mog::ErrorCode::Ok;
    case MOG_ERR_INVALID_URL:        return mog::ErrorCode::InvalidUrl;
    case MOG_ERR_INVALID_ARGUMENT:   return mog::ErrorCode::InvalidArgument;
    case MOG_ERR_UNSUPPORTED_SCHEME: return mog::ErrorCode::UnsupportedScheme;
    case MOG_ERR_UNSUPPORTED_BACKEND:return mog::ErrorCode::UnsupportedBackend;
    case MOG_ERR_DNS_FAILED:         return mog::ErrorCode::DnsFailed;
    case MOG_ERR_CONNECT_FAILED:     return mog::ErrorCode::ConnectFailed;
    case MOG_ERR_TLS_FAILED:         return mog::ErrorCode::TlsFailed;
    case MOG_ERR_TIMEOUT:            return mog::ErrorCode::Timeout;
    case MOG_ERR_IO:                 return mog::ErrorCode::IoError;
    case MOG_ERR_PROTOCOL:           return mog::ErrorCode::ProtocolError;
    case MOG_ERR_TOO_MANY_REDIRECTS: return mog::ErrorCode::TooManyRedirects;
    case MOG_ERR_HTTP_STATUS:        return mog::ErrorCode::HttpStatus;
    case MOG_ERR_RESPONSE_TOO_LARGE: return mog::ErrorCode::ResponseTooLarge;
    case MOG_ERR_PROXY:              return mog::ErrorCode::ProxyError;
    case MOG_ERR_FILE:               return mog::ErrorCode::FileError;
    case MOG_ERR_JSON:               return mog::ErrorCode::JsonError;
    case MOG_ERR_COMPRESSION:        return mog::ErrorCode::CompressionError;
    case MOG_ERR_DYNAMIC_LIBRARY:    return mog::ErrorCode::DynamicLibraryError;
    case MOG_ERR_INTERNAL:           return mog::ErrorCode::Internal;
    }
    return mog::ErrorCode::Internal;
}

bool IEquals(const std::string &a, const char *b) noexcept
{
    if (b == nullptr)
    {
        return false;
    }
    std::size_t i = 0;
    for (; i < a.size() && b[i] != '\0'; ++i)
    {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb))
        {
            return false;
        }
    }
    return i == a.size() && b[i] == '\0';
}

// Build a response handle carrying a synthesized error (used before a transfer
// even starts, e.g. an unparseable method). Returns nullptr on allocation
// failure, which the caller surfaces as NULL.
mog_response *MakeErrorResponse(mog::ErrorCode code, std::string message) noexcept
{
    auto *out = new (std::nothrow) mog_response;
    if (out == nullptr)
    {
        return nullptr;
    }
    out->ok = false;
    out->code = code;
    try
    {
        out->error_message = std::move(message);
    }
    catch (...)
    {
        // Leave the message empty; the code still carries the failure.
    }
    return out;
}

// Perform a prepared request and wrap the outcome. Never throws.
mog_response *PerformInternal(mog::Method method, const std::string &url,
                              const mog::Options &options) noexcept
{
    try
    {
        auto result = mog::request(method, url, options);
        auto *out = new (std::nothrow) mog_response;
        if (out == nullptr)
        {
            return nullptr;
        }
        if (result)
        {
            out->ok = true;
            out->code = mog::ErrorCode::Ok;
            out->resp = std::move(*result);
        }
        else
        {
            out->ok = false;
            out->code = result.error().code();
            out->error_message = result.error().to_string();
        }
        return out;
    }
    catch (const std::bad_alloc &)
    {
        return nullptr;
    }
    catch (const std::exception &ex)
    {
        return MakeErrorResponse(mog::ErrorCode::Internal, ex.what());
    }
    catch (...)
    {
        return MakeErrorResponse(mog::ErrorCode::Internal, "unknown error");
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Library information
// ---------------------------------------------------------------------------

extern "C" const char *mog_version(void)
{
    // mog::Version() returns a view over a static NUL-terminated string.
    static const std::string kVersion{mog::Version()};
    return kVersion.c_str();
}

extern "C" const char *mog_error_code_name(mog_error_code code)
{
    static thread_local std::string name;
    try
    {
        name = std::string{mog::ToString(FromCError(code))};
        return name.c_str();
    }
    catch (...)
    {
        return "Internal";
    }
}

// ---------------------------------------------------------------------------
// Request builder
// ---------------------------------------------------------------------------

extern "C" mog_request *mog_request_new(const char *method, const char *url)
{
    if (method == nullptr || url == nullptr || url[0] == '\0')
    {
        return nullptr;
    }
    auto parsed = mog::ParseMethod(method);
    if (!parsed)
    {
        return nullptr;
    }
    auto *req = new (std::nothrow) mog_request;
    if (req == nullptr)
    {
        return nullptr;
    }
    try
    {
        req->method = *parsed;
        req->url = url;
    }
    catch (...)
    {
        delete req;
        return nullptr;
    }
    return req;
}

extern "C" void mog_request_free(mog_request *req)
{
    delete req;
}

// Every setter tolerates a NULL handle and swallows the (practically only
// bad_alloc) exception a string/map insert could throw.
#define MOG_C_SETTER_BODY(expr)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if (req == nullptr)                                                                        \
        {                                                                                          \
            return;                                                                                \
        }                                                                                          \
        try                                                                                        \
        {                                                                                          \
            expr;                                                                                  \
        }                                                                                          \
        catch (...)                                                                                \
        {                                                                                          \
        }                                                                                          \
    } while (0)

extern "C" void mog_request_set_header(mog_request *req, const char *name, const char *value)
{
    if (name == nullptr)
    {
        return;
    }
    MOG_C_SETTER_BODY(req->options.headers[name] = (value != nullptr ? value : ""));
}

extern "C" void mog_request_set_body(mog_request *req, const void *data, size_t len)
{
    MOG_C_SETTER_BODY(req->options.body.assign(
        data != nullptr ? static_cast<const char *>(data) : "", data != nullptr ? len : 0));
}

extern "C" void mog_request_set_json(mog_request *req, const char *json)
{
    if (json == nullptr)
    {
        return;
    }
    MOG_C_SETTER_BODY(req->options.json = std::string{json});
}

extern "C" void mog_request_set_query_param(mog_request *req, const char *name, const char *value)
{
    if (name == nullptr)
    {
        return;
    }
    MOG_C_SETTER_BODY(req->options.params[name] = (value != nullptr ? value : ""));
}

extern "C" void mog_request_set_cookie(mog_request *req, const char *name, const char *value)
{
    if (name == nullptr)
    {
        return;
    }
    MOG_C_SETTER_BODY(req->options.cookies[name] = (value != nullptr ? value : ""));
}

extern "C" void mog_request_set_timeout_ms(mog_request *req, long milliseconds)
{
    MOG_C_SETTER_BODY(req->options.timeout = std::chrono::milliseconds{milliseconds});
}

extern "C" void mog_request_set_connect_timeout_ms(mog_request *req, long milliseconds)
{
    MOG_C_SETTER_BODY(req->options.connect_timeout = std::chrono::milliseconds{milliseconds});
}

extern "C" void mog_request_set_verify_tls(mog_request *req, int enable)
{
    MOG_C_SETTER_BODY(req->options.verify_tls = (enable != 0));
}

extern "C" void mog_request_set_ca_bundle(mog_request *req, const char *path)
{
    if (path == nullptr)
    {
        return;
    }
    MOG_C_SETTER_BODY(req->options.ca_bundle = std::string{path});
}

extern "C" void mog_request_set_client_cert(mog_request *req, const char *cert_path,
                                            const char *key_path, const char *key_password)
{
    if (cert_path == nullptr)
    {
        return;
    }
    MOG_C_SETTER_BODY({
        req->options.client_cert = std::string{cert_path};
        if (key_path != nullptr && key_path[0] != '\0')
        {
            req->options.client_key = std::string{key_path};
        }
        req->options.client_key_password = (key_password != nullptr ? key_password : "");
    });
}

extern "C" void mog_request_set_basic_auth(mog_request *req, const char *user, const char *password)
{
    MOG_C_SETTER_BODY(mog::WithBasicAuth(req->options, user != nullptr ? user : "",
                                         password != nullptr ? password : ""));
}

extern "C" void mog_request_set_bearer_token(mog_request *req, const char *token)
{
    MOG_C_SETTER_BODY(mog::WithBearerToken(req->options, token != nullptr ? token : ""));
}

extern "C" void mog_request_set_digest_auth(mog_request *req, const char *user, const char *password)
{
    MOG_C_SETTER_BODY(mog::WithDigestAuth(req->options, user != nullptr ? user : "",
                                          password != nullptr ? password : ""));
}

extern "C" void mog_request_set_proxy(mog_request *req, const char *proxy_url)
{
    if (proxy_url == nullptr)
    {
        return;
    }
    MOG_C_SETTER_BODY(req->options.proxy = std::string{proxy_url});
}

extern "C" void mog_request_set_backend(mog_request *req, const char *backend)
{
    if (backend == nullptr)
    {
        return;
    }
    auto parsed = mog::ParseBackend(backend);
    if (!parsed)
    {
        return;
    }
    MOG_C_SETTER_BODY(req->options.backend = *parsed);
}

extern "C" void mog_request_set_allow_redirects(mog_request *req, int enable)
{
    MOG_C_SETTER_BODY(req->options.allow_redirects = (enable != 0));
}

extern "C" void mog_request_set_max_redirects(mog_request *req, int max_redirects)
{
    MOG_C_SETTER_BODY(req->options.max_redirects = max_redirects);
}

extern "C" void mog_request_set_max_response_bytes(mog_request *req, size_t max_bytes)
{
    MOG_C_SETTER_BODY(req->options.max_response_bytes = max_bytes);
}

extern "C" void mog_request_set_decompress(mog_request *req, int enable)
{
    MOG_C_SETTER_BODY(req->options.decompress = (enable != 0));
}

extern "C" void mog_request_set_user_agent(mog_request *req, const char *user_agent)
{
    if (user_agent == nullptr)
    {
        return;
    }
    MOG_C_SETTER_BODY(req->options.user_agent = std::string{user_agent});
}

#undef MOG_C_SETTER_BODY

extern "C" mog_response *mog_perform(mog_request *req)
{
    if (req == nullptr)
    {
        return nullptr;
    }
    return PerformInternal(req->method, req->url, req->options);
}

// ---------------------------------------------------------------------------
// One-shot conveniences
// ---------------------------------------------------------------------------

extern "C" mog_response *mog_get(const char *url)
{
    if (url == nullptr || url[0] == '\0')
    {
        return nullptr;
    }
    try
    {
        return PerformInternal(mog::Method::Get, std::string{url}, mog::Options{});
    }
    catch (...)
    {
        return nullptr;
    }
}

extern "C" mog_response *mog_post(const char *url, const void *body, size_t len)
{
    if (url == nullptr || url[0] == '\0')
    {
        return nullptr;
    }
    try
    {
        mog::Options opt;
        if (body != nullptr && len > 0)
        {
            opt.body.assign(static_cast<const char *>(body), len);
        }
        return PerformInternal(mog::Method::Post, std::string{url}, opt);
    }
    catch (...)
    {
        return nullptr;
    }
}

// ---------------------------------------------------------------------------
// Response accessors
// ---------------------------------------------------------------------------

extern "C" int mog_response_ok(const mog_response *resp)
{
    return (resp != nullptr && resp->ok) ? 1 : 0;
}

extern "C" mog_error_code mog_response_error_code(const mog_response *resp)
{
    if (resp == nullptr)
    {
        return MOG_ERR_INVALID_ARGUMENT;
    }
    return ToCError(resp->code);
}

extern "C" const char *mog_response_error_message(const mog_response *resp)
{
    if (resp == nullptr)
    {
        return kEmpty;
    }
    return resp->error_message.c_str();
}

extern "C" int mog_response_status(const mog_response *resp)
{
    return resp != nullptr ? resp->resp.status_code : 0;
}

extern "C" const char *mog_response_reason(const mog_response *resp)
{
    return resp != nullptr ? resp->resp.reason.c_str() : kEmpty;
}

extern "C" const char *mog_response_url(const mog_response *resp)
{
    return resp != nullptr ? resp->resp.url.c_str() : kEmpty;
}

extern "C" const char *mog_response_body(const mog_response *resp, size_t *len_out)
{
    if (resp == nullptr)
    {
        if (len_out != nullptr)
        {
            *len_out = 0;
        }
        return kEmpty;
    }
    if (len_out != nullptr)
    {
        *len_out = resp->resp.body.size();
    }
    return resp->resp.body.c_str();
}

extern "C" size_t mog_response_body_size(const mog_response *resp)
{
    return resp != nullptr ? resp->resp.body.size() : 0;
}

extern "C" size_t mog_response_header_count(const mog_response *resp)
{
    return resp != nullptr ? resp->resp.headers.size() : 0;
}

extern "C" const char *mog_response_header_name(const mog_response *resp, size_t index)
{
    if (resp == nullptr || index >= resp->resp.headers.size())
    {
        return kEmpty;
    }
    return resp->resp.headers[index].name.c_str();
}

extern "C" const char *mog_response_header_value(const mog_response *resp, size_t index)
{
    if (resp == nullptr || index >= resp->resp.headers.size())
    {
        return kEmpty;
    }
    return resp->resp.headers[index].value.c_str();
}

extern "C" const char *mog_response_header(const mog_response *resp, const char *name)
{
    if (resp == nullptr || name == nullptr)
    {
        return kEmpty;
    }
    for (const auto &h : resp->resp.headers)
    {
        if (IEquals(h.name, name))
        {
            return h.value.c_str();
        }
    }
    return kEmpty;
}

extern "C" long mog_response_elapsed_ms(const mog_response *resp)
{
    return resp != nullptr ? static_cast<long>(resp->resp.elapsed.count()) : 0;
}

extern "C" size_t mog_response_downloaded_bytes(const mog_response *resp)
{
    return resp != nullptr ? resp->resp.downloaded_bytes : 0;
}

extern "C" const char *mog_response_backend(const mog_response *resp)
{
    return resp != nullptr ? resp->resp.backend.c_str() : kEmpty;
}

extern "C" void mog_response_free(mog_response *resp)
{
    delete resp;
}
