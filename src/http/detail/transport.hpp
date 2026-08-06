/**
 * @file transport.hpp
 * @brief HTTP transport backend interface (Open/Closed for new stacks).
 *
 * New backends (curl, WinHTTP, Apple NSURLSession, browser Fetch) implement @ref Transport and
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
     * @brief Whether this backend can correctly service a request with @p options.
     *
     * Under @c Auto, a request needing a feature the preferred native backend does
     * not implement (streaming @c response_writer, Digest auth, and for the
     * NSURLSession/WinHTTP backends a PEM CA bundle or client certificate, and
     * for NSURLSession an explicit proxy) transparently falls back to embedded
     * on native platforms. This is ignored when a backend is selected explicitly.
     */
    [[nodiscard]] virtual bool Supports(const Options & /*options*/) const noexcept
    {
        return true;
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
 * @brief The platform's preferred backend id (Web on Emscripten, Native on
 *        macOS, WinHttp on Windows, Curl on Linux, else Embedded) — regardless
 *        of availability.
 */
[[nodiscard]] Backend PreferredNativeBackend() noexcept;

/**
 * @brief Whether a concrete backend is registered and reports itself available.
 */
[[nodiscard]] bool IsBackendAvailable(Backend id);

/**
 * @brief Concrete backend that @c Auto should use (availability only).
 *
 * Prefers the platform backend when it is available and auto-preferred;
 * otherwise @c Embedded.
 * Does not consider per-request capability — see @ref SelectBackend.
 */
[[nodiscard]] Backend ResolveAutoBackend();

/**
 * @brief Choose the concrete backend for a specific request.
 *
 * Precedence: an explicit @c Options::backend (non-Auto) or @c MOG_BACKEND is
 * honored exactly. Otherwise (Auto) the platform backend is used when it
 * is available, auto-preferred, and @ref Transport::Supports the request; if any
 * of those fail, falls back to @c Embedded.
 */
[[nodiscard]] Backend SelectBackend(const Options &options);

} // namespace mog::detail
