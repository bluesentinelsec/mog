/**
 * @file backend.hpp
 * @brief HTTP transport backend selection.
 */
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace mog
{

/**
 * @brief Available HTTP transport backends.
 *
 * @c Auto resolves using environment override (if any) then the default
 * embedded stack. Platform-native backends (curl / WinHTTP / NSURLSession)
 * will be wired in later releases; requesting them today returns
 * @c ErrorCode::UnsupportedBackend unless implemented.
 */
enum class Backend
{
    Auto = 0,
    Embedded, ///< Built-in HTTP/1.1 + mbedTLS (default, always available).
    Curl,     ///< Runtime libcurl via dlopen (planned).
    WinHttp,  ///< Windows WinHTTP (planned).
    Native,   ///< OS-native (e.g. NSURLSession on macOS) (planned).
};

/**
 * @brief Parse a backend name (case-insensitive).
 * @param text One of: auto, embedded, curl, winhttp, native.
 * @return Backend or nullopt if unknown.
 */
[[nodiscard]] std::optional<Backend> ParseBackend(std::string_view text);

/**
 * @return Canonical lowercase name for @p backend.
 */
[[nodiscard]] std::string_view ToString(Backend backend) noexcept;

/**
 * @brief Resolve which backend will handle a request.
 *
 * Precedence (highest first):
 * 1. @p explicit_override when set (library Options or CLI --backend)
 * 2. Environment variable @c MOG_BACKEND
 * 3. @c Backend::Auto → @c Backend::Embedded (current default)
 *
 * @param explicit_override Optional caller override.
 * @return Concrete backend (never @c Auto).
 */
[[nodiscard]] Backend ResolveBackend(std::optional<Backend> explicit_override = std::nullopt);

/**
 * @brief Read @c MOG_BACKEND from the environment if set and valid.
 */
[[nodiscard]] std::optional<Backend> BackendFromEnvironment();

} // namespace mog
