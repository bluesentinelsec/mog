/**
 * @file prepare.cpp
 * @brief Normalize Options into wire headers + body.
 */

#include "http/detail/prepare.hpp"

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
    }
    else if (!options.form.empty())
    {
        out.body = EncodeForm(options.form);
        if (!HasHeader(out.headers, "Content-Type"))
        {
            out.headers["Content-Type"] = "application/x-www-form-urlencoded";
        }
    }
    else
    {
        out.body = options.body;
    }

    // Auth
    if (options.auth.kind == Auth::Kind::Basic)
    {
        if (!HasHeader(out.headers, "Authorization"))
        {
            const std::string token =
                Base64Encode(options.auth.username + ":" + options.auth.password);
            out.headers["Authorization"] = "Basic " + token;
        }
    }
    else if (options.auth.kind == Auth::Kind::Bearer)
    {
        if (!HasHeader(out.headers, "Authorization"))
        {
            out.headers["Authorization"] = "Bearer " + options.auth.token;
        }
    }

    // Cookies
    if (!options.cookies.empty() && !HasHeader(out.headers, "Cookie"))
    {
        out.headers["Cookie"] = EncodeCookieHeader(options.cookies);
    }

    // User-Agent default applied later in request builder if still missing;
    // set here only when provided on Options.
    if (!options.user_agent.empty() && !HasHeader(out.headers, "User-Agent"))
    {
        out.headers["User-Agent"] = options.user_agent;
    }

    return out;
}

} // namespace mog::detail
