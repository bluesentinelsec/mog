/**
 * @file transport.hpp
 * @brief HTTP transport backend interface (Open/Closed for new stacks).
 *
 * New backends (curl, WinHTTP, NSURLSession) implement @ref Transport and
 * register themselves; @c mog::request dispatches via the registry without
 * growing a central switch for each addition.
 */
#pragma once

#include "mog/backend.hpp"
#include "mog/options.hpp"
#include "mog/response.hpp"

#include <memory>
#include <string_view>

namespace mog::detail
{

/**
 * @brief One concrete HTTP stack (embedded, curl, …).
 */
class Transport
{
  public:
    virtual ~Transport() = default;

    [[nodiscard]] virtual std::string_view Name() const noexcept = 0;

    /**
     * @brief Whether this backend can actually service requests in this process.
     *
     * Native backends return false when their library/framework is missing at
     * runtime (e.g. libcurl not installed). @c Auto skips unavailable backends
     * and falls back to embedded. The embedded backend is always available.
     */
    [[nodiscard]] virtual bool Available() const noexcept
    {
        return true;
    }

    /**
     * @brief Whether @c Auto should prefer this backend over the embedded fallback.
     *
     * Being @ref Available (explicitly selectable + conformance-tested) is not
     * enough: a native backend becomes auto-preferred only once it reaches
     * feature parity and is meant to be the default. Defaults to false; the
     * embedded fallback need not set it.
     */
    [[nodiscard]] virtual bool AutoPreferred() const noexcept
    {
        return false;
    }

    /**
     * @brief Perform one logical request (including redirects for this stack).
     */
    [[nodiscard]] virtual Result<Response> Execute(Method method, std::string_view url,
                                                   const Options &options) = 0;
};

/**
 * @brief Register @p transport for @p id (overwrites if present).
 *
 * Thread-safe. Call during static init or process startup before concurrent use.
 */
void RegisterTransport(Backend id, std::unique_ptr<Transport> transport);

/**
 * @brief Lookup a registered transport for a concrete backend id (not Auto).
 * @return nullptr if not registered / not implemented.
 */
[[nodiscard]] Transport *FindTransport(Backend id) noexcept;

/**
 * @brief Ensure built-in transports are registered (idempotent, thread-safe).
 */
void EnsureDefaultTransportsRegistered();

/**
 * @brief The platform's preferred native backend id (Native on macOS, WinHttp on
 *        Windows, Curl on Linux, else Embedded) — regardless of availability.
 */
[[nodiscard]] Backend PreferredNativeBackend() noexcept;

/**
 * @brief Whether a concrete backend is registered and reports itself available.
 */
[[nodiscard]] bool IsBackendAvailable(Backend id);

/**
 * @brief Concrete backend that @c Auto should use.
 *
 * Prefers the platform-native backend (Native on macOS, WinHttp on Windows,
 * Curl on Linux) when it is available; otherwise falls back to @c Embedded.
 */
[[nodiscard]] Backend ResolveAutoBackend();

} // namespace mog::detail
