/**
 * @file cookie_jar.cpp
 * @brief Domain/path-aware cookie jar for Session (simplified RFC 6265).
 */

#include "http/detail/cookie_jar.hpp"

#include <algorithm>
#include <cctype>

namespace mog::detail
{
namespace
{

bool EqualsIgnoreCase(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
        {
            return false;
        }
    }
    return true;
}

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

std::string_view Trim(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
    {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
    {
        s.remove_suffix(1);
    }
    return s;
}

/**
 * @brief Parse one Set-Cookie value into a StoredCookie using request context.
 * @return false when the header has no valid name=value pair.
 */
bool ParseSetCookieValue(std::string_view header, const Url &request_url, StoredCookie &out)
{
    // First segment is name=value; remaining ';'-separated segments are attributes.
    std::vector<std::string_view> segments;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= header.size(); ++i)
    {
        if (i == header.size() || header[i] == ';')
        {
            segments.push_back(header.substr(start, i - start));
            start = i + 1;
        }
    }
    if (segments.empty())
    {
        return false;
    }

    const std::string_view nv = Trim(segments.front());
    const auto eq = nv.find('=');
    if (eq == std::string_view::npos || eq == 0)
    {
        return false;
    }
    out = StoredCookie{};
    out.name = std::string{Trim(nv.substr(0, eq))};
    out.value = std::string{Trim(nv.substr(eq + 1))};
    if (out.name.empty())
    {
        return false;
    }

    const std::string request_host = ToLower(request_url.host);
    out.domain = request_host;
    out.host_only = true;
    out.path = DefaultCookiePath(request_url.path);
    out.secure = false;

    for (std::size_t i = 1; i < segments.size(); ++i)
    {
        const std::string_view attr = Trim(segments[i]);
        if (attr.empty())
        {
            continue;
        }
        const auto aeq = attr.find('=');
        const std::string_view key =
            Trim(aeq == std::string_view::npos ? attr : attr.substr(0, aeq));
        const std::string_view val =
            aeq == std::string_view::npos ? std::string_view{} : Trim(attr.substr(aeq + 1));

        if (EqualsIgnoreCase(key, "Secure"))
        {
            out.secure = true;
        }
        else if (EqualsIgnoreCase(key, "Path"))
        {
            if (!val.empty() && val.front() == '/')
            {
                out.path = std::string{val};
            }
        }
        else if (EqualsIgnoreCase(key, "Domain"))
        {
            std::string_view d = val;
            while (!d.empty() && d.front() == '.') // ignore a leading dot
            {
                d.remove_prefix(1);
            }
            if (!d.empty())
            {
                const std::string domain = ToLower(d);
                // Only accept a Domain the request host actually belongs to; otherwise
                // fall back to a host-only cookie (avoids cross-site cookie setting).
                if (CookieDomainMatch(request_host, domain))
                {
                    out.domain = domain;
                    out.host_only = false;
                }
            }
        }
        // HttpOnly is intentionally ignored (we are not a browser DOM); Max-Age /
        // Expires / SameSite are non-goals (see cookie_jar.hpp).
    }

    return true;
}

} // namespace

std::string DefaultCookiePath(std::string_view request_path)
{
    if (request_path.empty() || request_path.front() != '/')
    {
        return "/";
    }
    const auto last_slash = request_path.find_last_of('/');
    if (last_slash == 0)
    {
        return "/";
    }
    return std::string{request_path.substr(0, last_slash)};
}

bool CookieDomainMatch(std::string_view host, std::string_view domain)
{
    if (host == domain)
    {
        return true;
    }
    // host is a subdomain of domain: host ends with "." + domain.
    if (host.size() > domain.size() + 1)
    {
        const std::size_t offset = host.size() - domain.size();
        if (host[offset - 1] == '.' && host.substr(offset) == domain)
        {
            return true;
        }
    }
    return false;
}

bool CookiePathMatch(std::string_view request_path, std::string_view cookie_path)
{
    if (cookie_path.empty())
    {
        return true;
    }
    if (request_path == cookie_path)
    {
        return true;
    }
    if (request_path.size() > cookie_path.size() &&
        request_path.substr(0, cookie_path.size()) == cookie_path)
    {
        // Either the cookie path ends in '/', or the next request-path char is '/'.
        return cookie_path.back() == '/' || request_path[cookie_path.size()] == '/';
    }
    return false;
}

void CookieJar::StoreFromResponse(const Url &request_url, const std::vector<Header> &headers)
{
    for (const auto &h : headers)
    {
        if (!EqualsIgnoreCase(h.name, "Set-Cookie"))
        {
            continue;
        }
        StoredCookie cookie;
        if (!ParseSetCookieValue(h.value, request_url, cookie))
        {
            continue;
        }
        // RFC 6265 identity is (name, domain, path): replace an existing match.
        auto it = std::find_if(cookies_.begin(), cookies_.end(), [&](const StoredCookie &c) {
            return c.name == cookie.name && c.domain == cookie.domain && c.path == cookie.path &&
                   c.host_only == cookie.host_only;
        });
        if (it != cookies_.end())
        {
            *it = std::move(cookie);
        }
        else
        {
            cookies_.push_back(std::move(cookie));
        }
    }
}

std::map<std::string, std::string> CookieJar::CookiesFor(const Url &request_url) const
{
    const std::string host = ToLower(request_url.host);
    const std::string &path = request_url.path;
    const bool https = request_url.scheme == "https";

    // Track the winning path length per name so the most specific cookie wins.
    std::map<std::string, std::string> out;
    std::map<std::string, std::size_t> best_path_len;

    for (const auto &c : cookies_)
    {
        bool domain_ok = false;
        if (c.domain.empty())
        {
            domain_ok = true; // manual, host-agnostic cookie
        }
        else if (c.host_only)
        {
            domain_ok = (host == c.domain);
        }
        else
        {
            domain_ok = CookieDomainMatch(host, c.domain);
        }
        if (!domain_ok)
        {
            continue;
        }
        if (!CookiePathMatch(path, c.path))
        {
            continue;
        }
        if (c.secure && !https)
        {
            continue;
        }

        auto existing = best_path_len.find(c.name);
        if (existing == best_path_len.end() || c.path.size() > existing->second)
        {
            out[c.name] = c.value;
            best_path_len[c.name] = c.path.size();
        }
    }
    return out;
}

void CookieJar::SetManual(const std::string &name, const std::string &value)
{
    auto it = std::find_if(cookies_.begin(), cookies_.end(), [&](const StoredCookie &c) {
        return c.name == name && c.domain.empty();
    });
    if (it != cookies_.end())
    {
        it->value = value;
        return;
    }
    StoredCookie c;
    c.name = name;
    c.value = value;
    c.domain.clear(); // any host
    c.path = "/";
    c.host_only = false;
    c.secure = false;
    cookies_.push_back(std::move(c));
}

std::map<std::string, std::string> CookieJar::AllNameValues() const
{
    std::map<std::string, std::string> out;
    std::map<std::string, std::size_t> best_path_len;
    for (const auto &c : cookies_)
    {
        auto existing = best_path_len.find(c.name);
        if (existing == best_path_len.end() || c.path.size() > existing->second)
        {
            out[c.name] = c.value;
            best_path_len[c.name] = c.path.size();
        }
    }
    return out;
}

void CookieJar::Clear()
{
    cookies_.clear();
}

} // namespace mog::detail
