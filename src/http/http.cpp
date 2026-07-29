/**
 * @file http.cpp
 * @brief Public free-function request API and backend dispatch.
 */

#include "mog/http.hpp"

#include "http/detail/embedded_backend.hpp"
#include "mog/backend.hpp"
#include "mog/log.hpp"

namespace mog
{
namespace
{

Result<Response> Dispatch(Method method, std::string_view url, const Options &options)
{
    const Backend backend = ResolveBackend(options.backend);
    MOG_LOG_INFO("request {} {} (backend={})", ToString(method), url, ToString(backend));
    switch (backend)
    {
    case Backend::Embedded:
    case Backend::Auto:
        // Auto should already be resolved; treat as embedded.
        return detail::EmbeddedRequest(method, url, options);
    case Backend::Curl:
        MOG_LOG_ERROR("backend curl not implemented");
        return Result<Response>::Err(Error{ErrorCode::UnsupportedBackend,
                                           "backend 'curl' is not implemented yet; use "
                                           "embedded (default) or set MOG_BACKEND=embedded"});
    case Backend::WinHttp:
        MOG_LOG_ERROR("backend winhttp not implemented");
        return Result<Response>::Err(
            Error{ErrorCode::UnsupportedBackend,
                  "backend 'winhttp' is not implemented yet; use embedded (default)"});
    case Backend::Native:
        MOG_LOG_ERROR("backend native not implemented");
        return Result<Response>::Err(
            Error{ErrorCode::UnsupportedBackend,
                  "backend 'native' is not implemented yet; use embedded (default)"});
    }
    return Result<Response>::Err(Error{ErrorCode::Internal, "unknown backend"});
}

} // namespace

Result<Response> request(Method method, std::string_view url, const Options &options)
{
    auto result = Dispatch(method, url, options);
    if (!result)
    {
        MOG_LOG_ERROR("request failed: {}", result.error().to_string());
    }
    else
    {
        MOG_LOG_INFO("response {} {} ({} bytes, {} ms, redirects={})", result->status_code,
                     result->url, result->body.size(), result->elapsed.count(),
                     result->history_len);
    }
    return result;
}

Result<Response> get(std::string_view url, const Options &options)
{
    return request(Method::Get, url, options);
}

Result<Response> post(std::string_view url, const Options &options)
{
    return request(Method::Post, url, options);
}

Result<Response> put(std::string_view url, const Options &options)
{
    return request(Method::Put, url, options);
}

Result<Response> patch(std::string_view url, const Options &options)
{
    return request(Method::Patch, url, options);
}

Result<Response> del(std::string_view url, const Options &options)
{
    return request(Method::Delete, url, options);
}

Result<Response> head(std::string_view url, const Options &options)
{
    return request(Method::Head, url, options);
}

Result<Response> options(std::string_view url, const Options &options)
{
    return request(Method::Options, url, options);
}

} // namespace mog
