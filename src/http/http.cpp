/**
 * @file http.cpp
 * @brief Public free-function request API and backend dispatch.
 */

#include "mog/http.hpp"

#include "http/detail/embedded_backend.hpp"
#include "mog/backend.hpp"

namespace mog
{
namespace
{

Result<Response> Dispatch(Method method, std::string_view url, const Options &options)
{
    const Backend backend = ResolveBackend(options.backend);
    switch (backend)
    {
    case Backend::Embedded:
    case Backend::Auto:
        // Auto should already be resolved; treat as embedded.
        return detail::EmbeddedRequest(method, url, options);
    case Backend::Curl:
        return Result<Response>::Err(Error{ErrorCode::UnsupportedBackend,
                                           "backend 'curl' is not implemented yet; use "
                                           "embedded (default) or set MOG_BACKEND=embedded"});
    case Backend::WinHttp:
        return Result<Response>::Err(
            Error{ErrorCode::UnsupportedBackend,
                  "backend 'winhttp' is not implemented yet; use embedded (default)"});
    case Backend::Native:
        return Result<Response>::Err(
            Error{ErrorCode::UnsupportedBackend,
                  "backend 'native' is not implemented yet; use embedded (default)"});
    }
    return Result<Response>::Err(Error{ErrorCode::Internal, "unknown backend"});
}

} // namespace

Result<Response> request(Method method, std::string_view url, const Options &options)
{
    return Dispatch(method, url, options);
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
