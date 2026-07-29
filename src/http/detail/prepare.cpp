/**
 * @file prepare.cpp
 * @brief Normalize Options into wire headers + body.
 */

#include "http/detail/prepare.hpp"

#include "mog/log.hpp"
#include "mog/util.hpp"

#include <cctype>

namespace mog::detail
{
namespace
{

bool HasHeader(const std::map<std::string, std::string> &headers, std::string_view name)
{
    for (const auto &h : headers)
    {
        if (h.first.size() != name.size())
        {
            continue;
        }
        bool eq = true;
        for (std::size_t i = 0; i < name.size(); ++i)
        {
            if (std::tolower(static_cast<unsigned char>(h.first[i])) !=
                std::tolower(static_cast<unsigned char>(name[i])))
            {
                eq = false;
                break;
            }
        }
        if (eq)
        {
            return true;
        }
    }
    return false;
}

} // namespace

std::chrono::milliseconds ConnectTimeout(const Options &options) noexcept
{
    if (options.connect_timeout.has_value())
    {
        return *options.connect_timeout;
    }
    return options.timeout;
}

std::chrono::milliseconds IoTimeout(const Options &options) noexcept
{
    return options.timeout;
}

PreparedRequest PrepareRequest(const Options &options)
{
    PreparedRequest out;
    out.headers = options.headers;

    // Body precedence: json > form > raw body
    if (options.json.has_value())
    {
        out.body = *options.json;
        if (!HasHeader(out.headers, "Content-Type"))
        {
            out.headers["Content-Type"] = "application/json";
        }
        MOG_LOG_DEBUG("prepare: json body ({} bytes)", out.body.size());
    }
    else if (!options.form.empty())
    {
        out.body = EncodeForm(options.form);
        if (!HasHeader(out.headers, "Content-Type"))
        {
            out.headers["Content-Type"] = "application/x-www-form-urlencoded";
        }
        MOG_LOG_DEBUG("prepare: form body ({} fields, {} bytes)", options.form.size(),
                      out.body.size());
    }
    else
    {
        out.body = options.body;
        if (!out.body.empty())
        {
            MOG_LOG_DEBUG("prepare: raw body ({} bytes)", out.body.size());
        }
    }

    // Auth (never log secrets)
    if (options.auth.kind == Auth::Kind::Basic)
    {
        if (!HasHeader(out.headers, "Authorization"))
        {
            const std::string token =
                Base64Encode(options.auth.username + ":" + options.auth.password);
            out.headers["Authorization"] = "Basic " + token;
        }
        MOG_LOG_DEBUG("prepare: basic auth (user={})", options.auth.username);
    }
    else if (options.auth.kind == Auth::Kind::Bearer)
    {
        if (!HasHeader(out.headers, "Authorization"))
        {
            out.headers["Authorization"] = "Bearer " + options.auth.token;
        }
        MOG_LOG_DEBUG("prepare: bearer auth (token set)");
    }

    // Cookies
    if (!options.cookies.empty() && !HasHeader(out.headers, "Cookie"))
    {
        out.headers["Cookie"] = EncodeCookieHeader(options.cookies);
        MOG_LOG_DEBUG("prepare: {} cookie(s)", options.cookies.size());
    }

    // User-Agent default applied later in request builder if still missing;
    // set here only when provided on Options.
    if (!options.user_agent.empty() && !HasHeader(out.headers, "User-Agent"))
    {
        out.headers["User-Agent"] = options.user_agent;
    }

    if (options.proxy.has_value())
    {
        MOG_LOG_DEBUG("prepare: proxy={}", *options.proxy);
    }
    MOG_LOG_DEBUG("prepare: headers={} timeout={}ms verify_tls={}", out.headers.size(),
                  options.timeout.count(), options.verify_tls);

    return out;
}

} // namespace mog::detail
