/**
 * @file session.cpp
 * @brief Thread-safe Session with cookie jar.
 */

#include "mog/session.hpp"

#include "mog/log.hpp"
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

void Session::set_basic_auth(std::string username, std::string password)
{
    std::lock_guard lock(mutex_);
    WithBasicAuth(defaults_, std::move(username), std::move(password));
}

void Session::set_bearer_token(std::string token)
{
    std::lock_guard lock(mutex_);
    WithBearerToken(defaults_, std::move(token));
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

void Session::set_cookies(std::map<std::string, std::string> cookies)
{
    std::lock_guard lock(mutex_);
    cookie_jar_ = std::move(cookies);
}

void Session::set_cookie(std::string name, std::string value)
{
    std::lock_guard lock(mutex_);
    cookie_jar_[std::move(name)] = std::move(value);
}

std::map<std::string, std::string> Session::cookies() const
{
    std::lock_guard lock(mutex_);
    return cookie_jar_;
}

void Session::clear_cookies()
{
    std::lock_guard lock(mutex_);
    cookie_jar_.clear();
}

Options Session::merge_options(const Options &per_request) const
{
    // Caller holds mutex_. Start from session defaults.
    Options merged = defaults_;

    for (const auto &h : per_request.headers)
    {
        merged.headers[h.first] = h.second;
    }
    for (const auto &p : per_request.params)
    {
        merged.params[p.first] = p.second;
    }
    for (const auto &c : per_request.cookies)
    {
        merged.cookies[c.first] = c.second;
    }
    for (const auto &f : per_request.form)
    {
        merged.form[f.first] = f.second;
    }

    // Cookie jar underlays per-request cookies (request wins on name clash).
    for (const auto &c : cookie_jar_)
    {
        if (merged.cookies.find(c.first) == merged.cookies.end())
        {
            merged.cookies[c.first] = c.second;
        }
    }

    if (per_request.json.has_value())
    {
        merged.json = per_request.json;
    }
    if (!per_request.body.empty())
    {
        merged.body = per_request.body;
    }
    if (per_request.ca_bundle.has_value())
    {
        merged.ca_bundle = per_request.ca_bundle;
    }
    if (per_request.backend.has_value())
    {
        merged.backend = per_request.backend;
    }
    if (per_request.proxy.has_value())
    {
        merged.proxy = per_request.proxy;
    }
    if (!per_request.user_agent.empty())
    {
        merged.user_agent = per_request.user_agent;
    }
    if (per_request.auth.kind != Auth::Kind::None)
    {
        merged.auth = per_request.auth;
    }
    if (per_request.connect_timeout.has_value())
    {
        merged.connect_timeout = per_request.connect_timeout;
    }

    const Options virgin{};
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
    if (per_request.max_response_bytes != virgin.max_response_bytes)
    {
        merged.max_response_bytes = per_request.max_response_bytes;
    }
    if (per_request.update_cookies != virgin.update_cookies)
    {
        merged.update_cookies = per_request.update_cookies;
    }
    if (per_request.decompress != virgin.decompress)
    {
        merged.decompress = per_request.decompress;
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
        MOG_LOG_DEBUG("session: {} {} (jar={} cookies, base={})", ToString(method), full_url,
                      cookie_jar_.size(), base_url_);
    }

    auto result = mog::request(method, full_url, merged);
    if (!result)
    {
        return result;
    }

    if (merged.update_cookies && !result->cookies.empty())
    {
        std::lock_guard lock(mutex_);
        for (const auto &c : result->cookies)
        {
            cookie_jar_[c.first] = c.second;
        }
        MOG_LOG_DEBUG("session: stored {} cookie(s) from response (jar size now {})",
                      result->cookies.size(), cookie_jar_.size());
    }
    return result;
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
