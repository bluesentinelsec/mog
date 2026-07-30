/**
 * @file cookie_jar.hpp
 * @brief Domain/path-aware cookie jar for Session (simplified RFC 6265).
 *
 * Good enough for typical login/session APIs — not a full browser jar. See
 * @ref CookieJar for the intentional non-goals (SameSite, public suffix list,
 * expiry eviction, redirect-chain capture).
 */
#pragma once

#include "http/detail/url.hpp"
#include "mog/response.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace mog::detail
{

/**
 * @brief One stored cookie with the attributes we honor.
 */
struct StoredCookie
{
    std::string name;
    std::string value;
    std::string domain;    ///< Canonical lowercase host (no leading dot). Empty = any host.
    std::string path;      ///< Cookie path ("/" default). Request path must path-match.
    bool host_only = true; ///< true when no Domain attribute (exact host match only).
    bool secure = false;   ///< Send only over HTTPS.
};

/**
 * @brief Cookie storage with domain/path/secure matching.
 *
 * Not internally synchronized — Session owns one and calls it under its mutex.
 *
 * Intentional non-goals (kept simple on purpose): full RFC 6265 semantics,
 * SameSite, public-suffix-list validation of Domain, expiry / Max-Age eviction
 * (all cookies are session-lifetime), and capturing Set-Cookie emitted on
 * intermediate redirect hops (only the final response is seen).
 */
class CookieJar
{
  public:
    /// Parse Set-Cookie headers from a response received from @p request_url and store them.
    void StoreFromResponse(const Url &request_url, const std::vector<Header> &headers);

    /// Name→value cookies to send to @p request_url (domain/path/secure filtered).
    [[nodiscard]] std::map<std::string, std::string> CookiesFor(const Url &request_url) const;

    /// Manually add a cookie that matches any host at path "/" (non-secure).
    void SetManual(const std::string &name, const std::string &value);

    /// Flatten every stored cookie to name→value (most-specific path wins on name clash).
    [[nodiscard]] std::map<std::string, std::string> AllNameValues() const;

    void Clear();

    [[nodiscard]] bool Empty() const noexcept
    {
        return cookies_.empty();
    }

  private:
    std::vector<StoredCookie> cookies_;
};

// --- Matching primitives (exposed for unit testing) ---

/// RFC 6265 §5.1.4 default-path derived from a request path.
[[nodiscard]] std::string DefaultCookiePath(std::string_view request_path);

/// RFC 6265 §5.1.3 domain-match (host equals domain or is a subdomain of it).
[[nodiscard]] bool CookieDomainMatch(std::string_view host, std::string_view domain);

/// RFC 6265 §5.1.4 path-match (request path is within cookie path).
[[nodiscard]] bool CookiePathMatch(std::string_view request_path, std::string_view cookie_path);

} // namespace mog::detail
