/**
 * @file socket.hpp
 * @brief Cross-platform TCP client socket.
 */
#pragma once

#include "mog/error.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace mog::detail
{

class TcpSocket
{
  public:
    TcpSocket() = default;
    ~TcpSocket();

    TcpSocket(const TcpSocket &) = delete;
    TcpSocket &operator=(const TcpSocket &) = delete;
    TcpSocket(TcpSocket &&other) noexcept;
    TcpSocket &operator=(TcpSocket &&other) noexcept;

    [[nodiscard]] static Result<TcpSocket> Connect(std::string_view host, std::uint16_t port,
                                                   std::chrono::milliseconds timeout);

    /**
     * @brief Take ownership of an already-connected native socket (e.g. from
     *        accept()) and put it in non-blocking mode.
     * @return An error if the descriptor cannot be configured.
     */
    [[nodiscard]] static Result<TcpSocket> Adopt(std::intptr_t fd);

    [[nodiscard]] Result<std::size_t> SendAll(const void *data, std::size_t len,
                                              std::chrono::milliseconds timeout);
    [[nodiscard]] Result<std::size_t> RecvSome(void *data, std::size_t len,
                                               std::chrono::milliseconds timeout);

    [[nodiscard]] bool valid() const noexcept;
    void Close() noexcept;

    /**
     * @return Native handle for TLS BIO callbacks (SOCKET on Windows, int elsewhere).
     */
    [[nodiscard]] std::intptr_t native_handle() const noexcept
    {
        return fd_;
    }

  private:
    explicit TcpSocket(std::intptr_t fd) : fd_(fd)
    {
    }

    std::intptr_t fd_{-1};
};

/** Process-wide Winsock / socket subsystem init (thread-safe, once). */
void EnsureSocketSubsystem();

} // namespace mog::detail
