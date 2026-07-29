/**
 * @file url.cpp
 * @brief URL parsing and query encoding.
 */

#include "http/detail/url.hpp"

#include <cctype>
#include <charconv>
#include <sstream>

namespace mog::detail
{
namespace
{

std::string ToLower(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char ch : text)
    {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

bool IsUnreserved(char ch)
{
    return (std::isalnum(static_cast<unsigned char>(ch)) != 0) || ch == '-' || ch == '_' ||
           ch == '.' || ch == '~';
}

std::string PercentEncode(std::string_view text)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size() * 3);
    for (const char ch : text)
    {
        const auto uch = static_cast<unsigned char>(ch);
        if (IsUnreserved(ch))
        {
            out.push_back(ch);
        }
        else
        {
            out.push_back('%');
            out.push_back(kHex[(uch >> 4U) & 0x0FU]);
            out.push_back(kHex[uch & 0x0FU]);
        }
    }
    return out;
}

} // namespace

Result<Url> ParseUrl(std::string_view input)
{
    if (input.empty())
    {
        return Result<Url>::Err(Error{ErrorCode::InvalidUrl, "empty URL"});
    }

    const auto scheme_end = input.find("://");
    if (scheme_end == std::string_view::npos || scheme_end == 0)
    {
        return Result<Url>::Err(
            Error{ErrorCode::InvalidUrl, "URL must include a scheme (http:// or https://)"});
    }

    Url url;
    url.scheme = ToLower(input.substr(0, scheme_end));
    if (url.scheme != "http" && url.scheme != "https")
    {
        return Result<Url>::Err(
            Error{ErrorCode::UnsupportedScheme, "unsupported URL scheme: " + url.scheme});
    }

    std::string_view rest = input.substr(scheme_end + 3);
    if (rest.empty())
    {
        return Result<Url>::Err(Error{ErrorCode::InvalidUrl, "missing host"});
    }

    // Strip fragment.
    const auto hash = rest.find('#');
    if (hash != std::string_view::npos)
    {
        rest = rest.substr(0, hash);
    }

    std::string_view authority = rest;
    std::string_view path_query;
    const auto slash = rest.find('/');
    const auto qmark_early = rest.find('?');
    std::size_t auth_end = rest.size();
    if (slash != std::string_view::npos)
    {
        auth_end = slash;
    }
    if (qmark_early != std::string_view::npos && qmark_early < auth_end)
    {
        auth_end = qmark_early;
    }
    authority = rest.substr(0, auth_end);
    path_query = rest.substr(auth_end);

    if (authority.empty())
    {
        return Result<Url>::Err(Error{ErrorCode::InvalidUrl, "missing host"});
    }

    // userinfo@host not required for v1; accept and strip userinfo if present.
    const auto at = authority.find('@');
    if (at != std::string_view::npos)
    {
        authority = authority.substr(at + 1);
    }

    std::string_view host_view = authority;
    std::uint16_t port = 0;
    if (!authority.empty() && authority.front() == '[')
    {
        // IPv6 literal
        const auto close = authority.find(']');
        if (close == std::string_view::npos)
        {
            return Result<Url>::Err(Error{ErrorCode::InvalidUrl, "invalid IPv6 host"});
        }
        host_view = authority.substr(1, close - 1);
        if (close + 1 < authority.size())
        {
            if (authority[close + 1] != ':')
            {
                return Result<Url>::Err(Error{ErrorCode::InvalidUrl, "invalid host/port"});
            }
            const auto port_view = authority.substr(close + 2);
            unsigned int parsed = 0;
            const auto *begin = port_view.data();
            const auto *end = begin + port_view.size();
            const auto [ptr, ec] = std::from_chars(begin, end, parsed);
            if (ec != std::errc{} || ptr != end || parsed == 0 || parsed > 65535U)
            {
                return Result<Url>::Err(Error{ErrorCode::InvalidUrl, "invalid port"});
            }
            port = static_cast<std::uint16_t>(parsed);
        }
    }
    else
    {
        const auto colon = authority.rfind(':');
        if (colon != std::string_view::npos)
        {
            host_view = authority.substr(0, colon);
            const auto port_view = authority.substr(colon + 1);
            if (port_view.empty())
            {
                return Result<Url>::Err(Error{ErrorCode::InvalidUrl, "invalid port"});
            }
            unsigned int parsed = 0;
            const auto *begin = port_view.data();
            const auto *end = begin + port_view.size();
            const auto [ptr, ec] = std::from_chars(begin, end, parsed);
            if (ec != std::errc{} || ptr != end || parsed == 0 || parsed > 65535U)
            {
                return Result<Url>::Err(Error{ErrorCode::InvalidUrl, "invalid port"});
            }
            port = static_cast<std::uint16_t>(parsed);
        }
    }

    if (host_view.empty())
    {
        return Result<Url>::Err(Error{ErrorCode::InvalidUrl, "missing host"});
    }

    url.host = std::string{host_view};
    if (port == 0)
    {
        url.port = (url.scheme == "https") ? static_cast<std::uint16_t>(443)
                                           : static_cast<std::uint16_t>(80);
    }
    else
    {
        url.port = port;
    }

    if (path_query.empty())
    {
        url.path = "/";
        return Result<Url>::Ok(std::move(url));
    }

    if (path_query.front() == '?')
    {
        url.path = "/";
        url.query = std::string{path_query.substr(1)};
        return Result<Url>::Ok(std::move(url));
    }

    const auto qmark = path_query.find('?');
    if (qmark == std::string_view::npos)
    {
        url.path = std::string{path_query};
    }
    else
    {
        url.path = std::string{path_query.substr(0, qmark)};
        url.query = std::string{path_query.substr(qmark + 1)};
    }
    if (url.path.empty())
    {
        url.path = "/";
    }
    return Result<Url>::Ok(std::move(url));
}

std::string BuildUrl(const Url &url)
{
    std::ostringstream oss;
    oss << url.scheme << "://" << url.host;
    if (!IsDefaultPort(url))
    {
        oss << ':' << url.port;
    }
    oss << (url.path.empty() ? "/" : url.path);
    if (!url.query.empty())
    {
        oss << '?' << url.query;
    }
    return oss.str();
}

bool IsDefaultPort(const Url &url) noexcept
{
    if (url.scheme == "http")
    {
        return url.port == 80;
    }
    if (url.scheme == "https")
    {
        return url.port == 443;
    }
    return false;
}

std::string JoinUrl(std::string_view base, std::string_view ref)
{
    if (ref.find("://") != std::string_view::npos)
    {
        return std::string{ref};
    }
    auto base_parsed = ParseUrl(base);
    if (!base_parsed)
    {
        return std::string{ref};
    }
    Url out = *base_parsed;
    if (!ref.empty() && ref.front() == '/')
    {
        // Absolute path on same origin; may include query.
        const auto q = ref.find('?');
        if (q == std::string_view::npos)
        {
            out.path = std::string{ref};
            out.query.clear();
        }
        else
        {
            out.path = std::string{ref.substr(0, q)};
            out.query = std::string{ref.substr(q + 1)};
        }
        return BuildUrl(out);
    }

    // Relative path — resolve against directory of base path.
    std::string dir = out.path;
    const auto slash = dir.find_last_of('/');
    if (slash == std::string::npos)
    {
        dir = "/";
    }
    else
    {
        dir = dir.substr(0, slash + 1);
    }
    std::string_view rel = ref;
    std::string rel_query;
    const auto q = rel.find('?');
    if (q != std::string_view::npos)
    {
        rel_query = std::string{rel.substr(q + 1)};
        rel = rel.substr(0, q);
    }
    out.path = dir + std::string{rel};
    out.query = std::move(rel_query);
    return BuildUrl(out);
}

std::string EncodeQuery(const std::map<std::string, std::string> &params)
{
    std::ostringstream oss;
    bool first = true;
    for (const auto &entry : params)
    {
        if (!first)
        {
            oss << '&';
        }
        first = false;
        oss << PercentEncode(entry.first) << '=' << PercentEncode(entry.second);
    }
    return oss.str();
}

std::string AppendQuery(std::string_view url, const std::map<std::string, std::string> &params)
{
    if (params.empty())
    {
        return std::string{url};
    }
    const std::string encoded = EncodeQuery(params);
    std::string out{url};
    if (out.find('?') == std::string::npos)
    {
        out.push_back('?');
    }
    else if (!out.empty() && out.back() != '?' && out.back() != '&')
    {
        out.push_back('&');
    }
    out += encoded;
    return out;
}

} // namespace mog::detail
