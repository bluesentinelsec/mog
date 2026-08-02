/**
 * @file tls_server.hpp
 * @brief Server-side mbedTLS: shared certificate context + per-connection session.
 */
#pragma once

#include "http/detail/socket.hpp"
#include "mog/error.hpp"

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>

namespace mog::detail
{

/**
 * @brief Immutable server certificate material shared across connections.
 *
 * Holds the certificate chain and private key as PEM in memory. Each connection
 * parses them into its own mbedTLS context, so no mutable TLS state is shared
 * between worker threads (safe without MBEDTLS_THREADING).
 */
class TlsServerContext
{
  public:
    /// Load a certificate chain and private key from PEM files.
    [[nodiscard]] static Result<TlsServerContext> FromFiles(const std::string &cert_path,
                                                            const std::string &key_path,
                                                            const std::string &key_password);

    /// Build a context from PEM already held in memory.
    [[nodiscard]] static Result<TlsServerContext> FromPem(std::string cert_pem, std::string key_pem,
                                                          std::string key_password);

    /**
     * @brief Generate an ephemeral self-signed certificate (EC P-256).
     * @param common_name Subject/issuer CN (e.g. "localhost").
     */
    [[nodiscard]] static Result<TlsServerContext> SelfSigned(const std::string &common_name);

    [[nodiscard]] const std::string &cert_pem() const noexcept
    {
        return cert_pem_;
    }
    [[nodiscard]] const std::string &key_pem() const noexcept
    {
        return key_pem_;
    }
    [[nodiscard]] const std::string &key_password() const noexcept
    {
        return key_password_;
    }

  private:
    std::string cert_pem_;
    std::string key_pem_;
    std::string key_password_;
};

/**
 * @brief One server-side TLS connection (owns its own mbedTLS state).
 */
class TlsServerSession
{
  public:
    TlsServerSession();
    ~TlsServerSession();

    TlsServerSession(const TlsServerSession &) = delete;
    TlsServerSession &operator=(const TlsServerSession &) = delete;
    TlsServerSession(TlsServerSession &&) noexcept;
    TlsServerSession &operator=(TlsServerSession &&) noexcept;

    /**
     * @brief Perform the server-side TLS handshake on @p socket.
     * @param socket Accepted connection (must outlive this session).
     */
    [[nodiscard]] Result<void> Handshake(TcpSocket &socket, const TlsServerContext &context,
                                         std::chrono::milliseconds timeout);

    [[nodiscard]] Result<std::size_t> Read(void *data, std::size_t len,
                                           std::chrono::milliseconds timeout);
    [[nodiscard]] Result<std::size_t> Write(const void *data, std::size_t len,
                                            std::chrono::milliseconds timeout);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mog::detail
