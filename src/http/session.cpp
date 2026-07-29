/**
 * @file session.cpp
 * @brief Thread-safe Session implementation.
 */

#include "mog/session.hpp"

#include "mog/version.hpp"

namespace mog
{

Session::Session()
{
    defaults_.user_agent = std::string("mog/") + std::string{mog::Version()};
}

Session::Session(Options defaults) : defaults_(std::move(defaults))
{
    if (defaults_.user_agent.empty())
    {
        defaults_.user_agent = std::string("mog/") + std::string{mog::Version()};
    }
}

void Session::set_defaults(Options defaults)
{
    std::lock_guard lock(mutex_);
    defaults_ = std::move(defaults);
}

Options Session::defaults() const
{
    std::lock_guard lock(mutex_);
    return defaults_;
}

void Session::set_header(std::string name, std::string value)
{
    std::lock_guard lock(mutex_);
    defaults_.headers[std::move(name)] = std::move(value);
}

void Session::set_base_url(std::string base_url)
{
    std::lock_guard lock(mutex_);
    base_url_ = std::move(base_url);
}

std::string Session::base_url() const
{
    std::lock_guard lock(mutex_);
    return base_url_;
}

Options Session::merge_options(const Options &per_request) const
{
    // Start from session defaults so set_defaults() is honored for scalars.
    // Per-request headers/body/params/backend/ca_bundle/user_agent layer on top.
    // To override timeout/verify/redirects per call, pass them via a custom Options
    // after copying session.defaults() and mutating (or call set_defaults).
    Options merged = defaults_;
    for (const auto &h : per_request.headers)
    {
        merged.headers[h.first] = h.second;
    }
    if (!per_request.body.empty())
    {
        merged.body = per_request.body;
    }
    for (const auto &p : per_request.params)
    {
        merged.params[p.first] = p.second;
    }
    if (per_request.ca_bundle.has_value())
    {
        merged.ca_bundle = per_request.ca_bundle;
    }
    if (per_request.backend.has_value())
    {
        merged.backend = per_request.backend;
    }
    if (!per_request.user_agent.empty())
    {
        merged.user_agent = per_request.user_agent;
    }
    // Allow per-request override of common scalars when they differ from the
    // default-constructed Options values *and* from session defaults — actually
    // always apply timeout/verify/redirects from per_request when the caller
    // constructed Options intentionally. Free-function style: copy defaults first.
    // Policy: if per_request looks like a pure default Options (no headers/body/params
    // and default timeout), keep session scalars. Otherwise apply per-request scalars.
    const Options virgin{};
    const bool has_request_specific =
        !per_request.headers.empty() || !per_request.body.empty() || !per_request.params.empty() ||
        per_request.backend.has_value() || per_request.ca_bundle.has_value() ||
        !per_request.user_agent.empty() || per_request.timeout != virgin.timeout ||
        per_request.verify_tls != virgin.verify_tls ||
        per_request.allow_redirects != virgin.allow_redirects ||
        per_request.max_redirects != virgin.max_redirects;
    if (has_request_specific)
    {
        // Apply scalar overrides from per_request when any field was customized.
        if (per_request.timeout != virgin.timeout)
        {
            merged.timeout = per_request.timeout;
        }
        if (per_request.verify_tls != virgin.verify_tls)
        {
            merged.verify_tls = per_request.verify_tls;
        }
        if (per_request.allow_redirects != virgin.allow_redirects)
        {
            merged.allow_redirects = per_request.allow_redirects;
        }
        if (per_request.max_redirects != virgin.max_redirects)
        {
            merged.max_redirects = per_request.max_redirects;
        }
    }
    return merged;
}

std::string Session::resolve_url(std::string_view url) const
{
    if (base_url_.empty())
    {
        return std::string{url};
    }
    if (url.find("://") != std::string_view::npos)
    {
        return std::string{url};
    }
    if (!url.empty() && url.front() == '/')
    {
        // base + absolute path
        std::string base = base_url_;
        while (!base.empty() && base.back() == '/')
        {
            base.pop_back();
        }
        return base + std::string{url};
    }
    std::string base = base_url_;
    if (!base.empty() && base.back() != '/')
    {
        base.push_back('/');
    }
    return base + std::string{url};
}

Result<Response> Session::request(Method method, std::string_view url, const Options &options)
{
    Options merged;
    std::string full_url;
    {
        std::lock_guard lock(mutex_);
        merged = merge_options(options);
        full_url = resolve_url(url);
    }
    return mog::request(method, full_url, merged);
}

Result<Response> Session::get(std::string_view url, const Options &options)
{
    return request(Method::Get, url, options);
}

Result<Response> Session::post(std::string_view url, const Options &options)
{
    return request(Method::Post, url, options);
}

Result<Response> Session::put(std::string_view url, const Options &options)
{
    return request(Method::Put, url, options);
}

Result<Response> Session::patch(std::string_view url, const Options &options)
{
    return request(Method::Patch, url, options);
}

Result<Response> Session::del(std::string_view url, const Options &options)
{
    return request(Method::Delete, url, options);
}

Result<Response> Session::head(std::string_view url, const Options &options)
{
    return request(Method::Head, url, options);
}

Result<Response> Session::options(std::string_view url, const Options &options)
{
    return request(Method::Options, url, options);
}

} // namespace mog
