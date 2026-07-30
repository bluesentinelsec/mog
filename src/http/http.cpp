/**
 * @file http.cpp
 * @brief Public free-function request API; dispatches via transport registry.
 */

#include "mog/http.hpp"

#include "http/detail/transport.hpp"
#include "mog/backend.hpp"
#include "mog/log.hpp"

namespace mog
{

Result<Response> request(Method method, std::string_view url, const Options &options)
{
    detail::EnsureDefaultTransportsRegistered();

    // Auto prefers the platform-native backend when it can serve this request,
    // else falls back to embedded (capability-aware); explicit selection is exact.
    const Backend backend = detail::SelectBackend(options);
    MOG_LOG_INFO("request {} {} (backend={})", ToString(method), url, ToString(backend));

    detail::Transport *transport = detail::FindTransport(backend);
    if (transport == nullptr)
    {
        // Auto should have resolved; treat unknown as internal.
        MOG_LOG_ERROR("no transport registered for backend {}", ToString(backend));
        return Result<Response>::Err(
            Error{ErrorCode::Internal,
                  std::string("no transport for backend ") + std::string{ToString(backend)}});
    }

    auto result = transport->Execute(method, url, options);
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
