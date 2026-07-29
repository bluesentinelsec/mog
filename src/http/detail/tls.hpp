/**
 * @file tls.hpp
 * @brief mbedTLS client session over TcpSocket.
 */
#pragma once

#include "http/detail/socket.hpp"
#include "mog/error.hpp"

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace mog::detail
{

class TlsSession
{
  public:
    TlsSession();
    ~TlsSession();

    TlsSession(const TlsSession &) = delete;
    TlsSession &operator=(const TlsSession &) = delete;
    TlsSession(TlsSession &&) noexcept;
    TlsSession &operator=(TlsSession &&) noexcept;

    /**
     * @brief Perform TLS handshake as client.
     * @param socket Connected TCP socket (ownership retained by caller; must outlive session).
     * @param hostname SNI / cert verification hostname.
     * @param verify Verify server certificate when true.
     * @param ca_bundle Optional PEM file path; otherwise system defaults are tried.
     */
    [[nodiscard]] Result<void> Handshake(TcpSocket &socket, std::string_view hostname, bool verify,
                                         const std::optional<std::string> &ca_bundle,
                                         std::chrono::milliseconds timeout);

    [[nodiscard]] Result<std::size_t> Write(const void *data, std::size_t len,
                                            std::chrono::milliseconds timeout);
    [[nodiscard]] Result<std::size_t> Read(void *data, std::size_t len,
                                           std::chrono::milliseconds timeout);

    /**
     * @brief Point BIO callbacks at a stable socket address (call after moves).
     */
    void AttachSocket(TcpSocket *socket) noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mog::detail
