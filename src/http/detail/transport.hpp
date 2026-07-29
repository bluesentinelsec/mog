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

} // namespace mog::detail
