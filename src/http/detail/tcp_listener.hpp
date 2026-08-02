/**
 * @file tcp_listener.hpp
 * @brief Cross-platform TCP listening socket (server side of TcpSocket).
 */
#pragma once

#include "http/detail/socket.hpp"
#include "mog/error.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace mog::detail
{

/**
 * @brief A bound, listening TCP socket that accepts client connections.
 *
 * Accept() waits up to a caller-supplied timeout so an accept loop can poll a
 * shutdown flag between attempts. The listener is set non-blocking; each
 * accepted connection is returned as a non-blocking @ref TcpSocket.
 */
class TcpListener
{
  public:
    TcpListener() = default;
    ~TcpListener();

    TcpListener(const TcpListener &) = delete;
    TcpListener &operator=(const TcpListener &) = delete;
    TcpListener(TcpListener &&other) noexcept;
    TcpListener &operator=(TcpListener &&other) noexcept;

    /**
     * @brief Bind @p address:@p port and start listening.
     * @param address Interface to bind ("127.0.0.1", "0.0.0.0", "::", ...).
     * @param port    Port, or 0 to let the OS choose (read back via @ref port).
     * @param backlog listen() backlog.
     */
    [[nodiscard]] static Result<TcpListener> Bind(std::string_view address, std::uint16_t port,
                                                  int backlog);

    /**
     * @brief Accept the next connection, waiting up to @p timeout.
     * @return The connection; @c ErrorCode::Timeout if none arrived in time.
     */
    [[nodiscard]] Result<TcpSocket> Accept(std::chrono::milliseconds timeout,
                                           std::string *peer_address = nullptr);

    /// The actual bound port (meaningful after binding with port 0).
    [[nodiscard]] std::uint16_t port() const noexcept
    {
        return port_;
    }

    [[nodiscard]] bool valid() const noexcept;
    void Close() noexcept;

  private:
    explicit TcpListener(std::intptr_t fd, std::uint16_t port) : fd_(fd), port_(port)
    {
    }

    std::intptr_t fd_{-1};
    std::uint16_t port_{0};
};

} // namespace mog::detail
