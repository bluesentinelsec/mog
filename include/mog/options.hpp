/**
 * @file options.hpp
 * @brief Per-request options (requests-style kwargs).
 */
#pragma once

#include "mog/backend.hpp"

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <string_view>

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
 * @brief Per-request configuration (similar to kwargs in Python requests).
 */
struct Options
{
    /// Extra headers (merged over session defaults; these win on conflict).
    std::map<std::string, std::string> headers;

    /// Raw request body (for POST/PUT/PATCH).
    std::string body;

    /// Query parameters appended to the URL.
    std::map<std::string, std::string> params;

    /// Overall I/O deadline per connection phase (connect + read).
    std::chrono::milliseconds timeout{std::chrono::seconds(30)};

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
};

} // namespace mog
