/**
 * @file stream.hpp
 * @brief Byte stream over plain TCP or TLS.
 */
#pragma once

#include "http/detail/socket.hpp"
#include "http/detail/tls.hpp"
#include "mog/error.hpp"

#include <chrono>
#include <cstddef>
#include <string>
#include <utility>

namespace mog::detail
{

class Stream
{
  public:
    explicit Stream(TcpSocket socket) : socket_(std::move(socket))
    {
    }

    Stream(TcpSocket socket, TlsSession tls)
        : socket_(std::move(socket)), tls_(std::move(tls)), use_tls_(true)
    {
        // BIO was bound to the pre-move socket address; re-bind to our stable member.
        tls_.AttachSocket(&socket_);
    }

    [[nodiscard]] Result<void> WriteAll(const void *data, std::size_t len,
                                        std::chrono::milliseconds timeout)
    {
        if (use_tls_)
        {
            auto r = tls_.Write(data, len, timeout);
            if (!r)
            {
                return Result<void>::Err(r.error());
            }
            return Result<void>::Ok();
        }
        auto r = socket_.SendAll(data, len, timeout);
        if (!r)
        {
            return Result<void>::Err(r.error());
        }
        return Result<void>::Ok();
    }

    [[nodiscard]] Result<std::size_t> ReadSome(void *data, std::size_t len,
                                               std::chrono::milliseconds timeout)
    {
        if (use_tls_)
        {
            return tls_.Read(data, len, timeout);
        }
        return socket_.RecvSome(data, len, timeout);
    }

    [[nodiscard]] Result<void> WriteString(const std::string &s, std::chrono::milliseconds timeout)
    {
        return WriteAll(s.data(), s.size(), timeout);
    }

    /**
     * @brief Release the underlying TCP socket (plain streams only).
     *
     * Used after an HTTP CONNECT tunnel so TLS can be layered on the same socket.
     * Invalidates this Stream.
     */
    [[nodiscard]] TcpSocket ReleaseSocket()
    {
        use_tls_ = false;
        tls_ = TlsSession{};
        return std::move(socket_);
    }

  private:
    TcpSocket socket_;
    TlsSession tls_{};
    bool use_tls_ = false;
};

} // namespace mog::detail
