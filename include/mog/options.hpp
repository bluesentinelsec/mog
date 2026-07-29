/**
 * @file options.hpp
 * @brief Per-request options (requests-style kwargs) for web client workloads.
 */
#pragma once

#include "mog/backend.hpp"

#include <chrono>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mog
{

/**
 * @brief HTTP methods supported by the client.
 */
enum class Method
{
    Get,
    Post,
    Put,
    Patch,
    Delete,
    Head,
    Options,
};

/**
 * @return Wire token for @p method (e.g. "GET").
 */
[[nodiscard]] std::string_view ToString(Method method) noexcept;

/**
 * @brief Parse method name (case-insensitive).
 */
[[nodiscard]] std::optional<Method> ParseMethod(std::string_view text);

/**
 * @brief HTTP authentication to attach to a request.
 *
 * Prefer the helpers @ref WithBasicAuth and @ref WithBearerToken.
 */
struct Auth
{
    enum class Kind
    {
        None,
        Basic,
        Bearer,
    };

    Kind kind = Kind::None;
    std::string username; ///< Basic auth user.
    std::string password; ///< Basic auth password.
    std::string token;    ///< Bearer token (without the "Bearer " prefix).
};

/**
 * @brief Per-request configuration (similar to kwargs in Python requests).
 *
 * Only fields you set affect the request; Session defaults fill the rest when
 * using @ref Session.
 */
struct Options
{
    /// Extra headers (merged over session defaults; these win on conflict).
    std::map<std::string, std::string> headers;

    /// Raw request body. Ignored when @ref json or @ref form is set (those win).
    std::string body;

    /// If set, sent as the body with Content-Type: application/json (unless overridden).
    std::optional<std::string> json;

    /// If non-empty, encoded as application/x-www-form-urlencoded body (unless overridden).
    std::map<std::string, std::string> form;

    /// Query parameters appended to the URL.
    std::map<std::string, std::string> params;

    /// Cookies sent on this request (name → value). Merged with Session jar when using Session.
    std::map<std::string, std::string> cookies;

    /// Overall I/O deadline for connect (if connect_timeout unset) and for each read/write.
    std::chrono::milliseconds timeout{std::chrono::seconds(30)};

    /// Optional connect-only deadline; defaults to @ref timeout when unset.
    std::optional<std::chrono::milliseconds> connect_timeout;

    /// Verify TLS certificates (HTTPS). Set false only for debugging.
    bool verify_tls = true;

    /// Optional path to a PEM CA bundle (overrides default discovery).
    std::optional<std::string> ca_bundle;

    /// Follow 3xx responses with a Location header.
    bool allow_redirects = true;

    /// Maximum redirects when @ref allow_redirects is true.
    int max_redirects = 5;

    /// Backend override (CLI / Options). Env @c MOG_BACKEND is consulted if unset.
    std::optional<Backend> backend;

    /// User-Agent header when the caller does not set one.
    std::string user_agent;

    /// HTTP authentication (Basic or Bearer).
    Auth auth{};

    /**
     * @brief HTTP proxy URL, e.g. "http://127.0.0.1:8080".
     *
     * Used for both http and https targets (HTTPS uses CONNECT).
     * Empty / unset means direct connection.
     */
    std::optional<std::string> proxy;

    /// Hard cap on response body size (bytes). 0 = unlimited.
    std::size_t max_response_bytes = 64ULL * 1024ULL * 1024ULL;

    /// When true, Session updates its cookie jar from Set-Cookie (library default true on Session).
    bool update_cookies = true;
};

// ---------------------------------------------------------------------------
// Fluent helpers (mutate and return reference for chaining)
// ---------------------------------------------------------------------------

inline Options &WithHeader(Options &opt, std::string name, std::string value)
{
    opt.headers[std::move(name)] = std::move(value);
    return opt;
}

inline Options &WithBasicAuth(Options &opt, std::string username, std::string password)
{
    opt.auth.kind = Auth::Kind::Basic;
    opt.auth.username = std::move(username);
    opt.auth.password = std::move(password);
    opt.auth.token.clear();
    return opt;
}

inline Options &WithBearerToken(Options &opt, std::string token)
{
    opt.auth.kind = Auth::Kind::Bearer;
    opt.auth.token = std::move(token);
    opt.auth.username.clear();
    opt.auth.password.clear();
    return opt;
}

/**
 * @brief Set raw JSON text as the body (Content-Type applied at send time).
 *
 * When MOG_WITH_JSON is enabled, @c WithJson also accepts @c nlohmann::json
 * (see mog/json.hpp). String / string_view / C-string overloads are provided so
 * string literals are not ambiguous with nlohmann's converting constructors.
 */
inline Options &WithJson(Options &opt, std::string json_body)
{
    opt.json = std::move(json_body);
    return opt;
}

inline Options &WithJson(Options &opt, std::string_view json_body)
{
    opt.json = std::string{json_body};
    return opt;
}

inline Options &WithJson(Options &opt, const char *json_body)
{
    opt.json = json_body != nullptr ? std::string{json_body} : std::string{};
    return opt;
}

inline Options &WithForm(Options &opt, std::map<std::string, std::string> fields)
{
    opt.form = std::move(fields);
    return opt;
}

inline Options &WithProxy(Options &opt, std::string proxy_url)
{
    opt.proxy = std::move(proxy_url);
    return opt;
}

inline Options &WithTimeout(Options &opt, std::chrono::milliseconds timeout)
{
    opt.timeout = timeout;
    return opt;
}

/**
 * @brief Build Options for a JSON POST/PUT body in one shot (raw text).
 */
[[nodiscard]] inline Options JsonOptions(std::string json_body)
{
    Options opt;
    opt.json = std::move(json_body);
    return opt;
}

[[nodiscard]] inline Options JsonOptions(std::string_view json_body)
{
    Options opt;
    opt.json = std::string{json_body};
    return opt;
}

[[nodiscard]] inline Options JsonOptions(const char *json_body)
{
    Options opt;
    opt.json = json_body != nullptr ? std::string{json_body} : std::string{};
    return opt;
}

/**
 * @brief Build Options for form-urlencoded body.
 */
[[nodiscard]] inline Options FormOptions(std::map<std::string, std::string> fields)
{
    Options opt;
    opt.form = std::move(fields);
    return opt;
}

} // namespace mog
