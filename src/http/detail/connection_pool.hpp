/**
 * @file connection_pool.hpp
 * @brief Idle TCP/TLS stream pool for HTTP/1.1 keep-alive (Session).
 *
 * One idle stream per origin key (scheme/host/port/proxy). Thread-safe.
 * Free-function requests do not use a pool (connection-per-request).
 */
#pragma once

#include "http/detail/stream.hpp"
#include "http/detail/url.hpp"
#include "mog/response.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mog::detail
{

/**
 * @brief Origin + proxy identity for pooled connections.
 */
struct ConnectionKey
{
    std::string scheme;
    std::string host;
    std::uint16_t port = 0;
    std::string proxy; ///< Empty when not using a proxy.

    [[nodiscard]] std::string ToString() const;
    [[nodiscard]] bool operator==(const ConnectionKey &other) const noexcept;
};

/**
 * @brief Hash for @ref ConnectionKey (used by the idle map).
 */
struct ConnectionKeyHash
{
    [[nodiscard]] std::size_t operator()(const ConnectionKey &k) const noexcept;
};

/**
 * @brief Small idle connection pool (at most one stream per key).
 *
 * Model: Session owns a shared pool; concurrent requests may open parallel
 * connections, but only one idle stream is retained per origin when released.
 */
class ConnectionPool
{
  public:
    ConnectionPool() = default;
    ConnectionPool(const ConnectionPool &) = delete;
    ConnectionPool &operator=(const ConnectionPool &) = delete;

    /**
     * @brief Remove and return an idle stream for @p key, or nullptr if none.
     */
    [[nodiscard]] std::unique_ptr<Stream> Take(const ConnectionKey &key);

    /**
     * @brief Return a stream to the idle pool (replaces any existing idle entry).
     */
    void Put(const ConnectionKey &key, std::unique_ptr<Stream> stream);

    /**
     * @brief Drop any idle stream for @p key (e.g. after protocol error).
     */
    void Drop(const ConnectionKey &key);

    /**
     * @return Number of idle streams currently held (for tests).
     */
    [[nodiscard]] std::size_t idle_count() const;

  private:
    mutable std::mutex mutex_;
    std::unordered_map<ConnectionKey, std::unique_ptr<Stream>, ConnectionKeyHash> idle_;
};

/**
 * @brief Build a pool key from a target URL and optional proxy string.
 */
[[nodiscard]] ConnectionKey MakeConnectionKey(const Url &url,
                                              const std::optional<std::string> &proxy);

/**
 * @brief Whether a response allows reusing the connection (HTTP/1.1 keep-alive).
 */
[[nodiscard]] bool ResponseAllowsKeepAlive(std::string_view status_line_version,
                                           const std::vector<Header> &headers) noexcept;

} // namespace mog::detail
