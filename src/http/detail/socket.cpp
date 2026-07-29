/**
 * @file socket.cpp
 * @brief Cross-platform TCP client socket.
 */

#include "http/detail/socket.hpp"

#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
using socklen_type = int;
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
using socklen_type = socklen_t;
#endif

namespace mog::detail
{
namespace
{

#if defined(_WIN32)
constexpr std::intptr_t kInvalid = static_cast<std::intptr_t>(INVALID_SOCKET);

int LastSocketError()
{
    return WSAGetLastError();
}

bool WouldBlock(int err)
{
    return err == WSAEWOULDBLOCK || err == WSAEINPROGRESS;
}
#else
constexpr std::intptr_t kInvalid = -1;

int LastSocketError()
{
    return errno;
}

bool WouldBlock(int err)
{
    return err == EWOULDBLOCK || err == EAGAIN || err == EINPROGRESS;
}
#endif

std::string SocketErrorMessage(const char *prefix)
{
    const int err = LastSocketError();
#if defined(_WIN32)
    return std::string(prefix) + " (code " + std::to_string(err) + ")";
#else
    return std::string(prefix) + ": " + std::strerror(err);
#endif
}

bool SetNonBlocking(std::intptr_t fd, bool enabled)
{
#if defined(_WIN32)
    u_long mode = enabled ? 1UL : 0UL;
    return ioctlsocket(static_cast<SOCKET>(fd), FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(static_cast<int>(fd), F_GETFL, 0);
    if (flags < 0)
    {
        return false;
    }
    if (enabled)
    {
        return fcntl(static_cast<int>(fd), F_SETFL, flags | O_NONBLOCK) == 0;
    }
    return fcntl(static_cast<int>(fd), F_SETFL, flags & ~O_NONBLOCK) == 0;
#endif
}

enum class WaitMode
{
    Read,
    Write,
};

Result<void> WaitReady(std::intptr_t fd, WaitMode mode, std::chrono::milliseconds timeout)
{
    if (timeout.count() < 0)
    {
        timeout = std::chrono::milliseconds(0);
    }
#if defined(_WIN32)
    fd_set set;
    FD_ZERO(&set);
    FD_SET(static_cast<SOCKET>(fd), &set);
    TIMEVAL tv{};
    tv.tv_sec = static_cast<long>(timeout.count() / 1000);
    tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);
    const int rc = select(0, mode == WaitMode::Read ? &set : nullptr,
                          mode == WaitMode::Write ? &set : nullptr, nullptr, &tv);
    if (rc == 0)
    {
        return Result<void>::Err(Error{ErrorCode::Timeout, "socket timed out"});
    }
    if (rc < 0)
    {
        return Result<void>::Err(Error{ErrorCode::IoError, SocketErrorMessage("select failed")});
    }
    return Result<void>::Ok();
#else
    pollfd pfd{};
    pfd.fd = static_cast<int>(fd);
    pfd.events = (mode == WaitMode::Read) ? POLLIN : POLLOUT;
    const int rc = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
    if (rc == 0)
    {
        return Result<void>::Err(Error{ErrorCode::Timeout, "socket timed out"});
    }
    if (rc < 0)
    {
        return Result<void>::Err(Error{ErrorCode::IoError, SocketErrorMessage("poll failed")});
    }
    return Result<void>::Ok();
#endif
}

} // namespace

void EnsureSocketSubsystem()
{
#if defined(_WIN32)
    static std::once_flag once;
    std::call_once(once, []() {
        WSADATA data{};
        const int rc = WSAStartup(MAKEWORD(2, 2), &data);
        if (rc != 0)
        {
            // Soft failure; Connect will surface errors.
        }
    });
#endif
}

TcpSocket::~TcpSocket()
{
    Close();
}

TcpSocket::TcpSocket(TcpSocket &&other) noexcept : fd_(other.fd_)
{
    other.fd_ = kInvalid;
}

TcpSocket &TcpSocket::operator=(TcpSocket &&other) noexcept
{
    if (this != &other)
    {
        Close();
        fd_ = other.fd_;
        other.fd_ = kInvalid;
    }
    return *this;
}

bool TcpSocket::valid() const noexcept
{
    return fd_ != kInvalid;
}

void TcpSocket::Close() noexcept
{
    if (!valid())
    {
        return;
    }
#if defined(_WIN32)
    closesocket(static_cast<SOCKET>(fd_));
#else
    ::close(static_cast<int>(fd_));
#endif
    fd_ = kInvalid;
}

Result<TcpSocket> TcpSocket::Connect(std::string_view host, std::uint16_t port,
                                     std::chrono::milliseconds timeout)
{
    EnsureSocketSubsystem();

    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    hints.ai_protocol = IPPROTO_TCP;

    const std::string host_str{host};
    const std::string port_str = std::to_string(port);
    addrinfo *result = nullptr;
    const int gai = ::getaddrinfo(host_str.c_str(), port_str.c_str(), &hints, &result);
    if (gai != 0)
    {
#if defined(_WIN32)
        return Result<TcpSocket>::Err(
            Error{ErrorCode::DnsFailed, "DNS lookup failed for " + host_str});
#else
        return Result<TcpSocket>::Err(
            Error{ErrorCode::DnsFailed, std::string("DNS lookup failed: ") + gai_strerror(gai)});
#endif
    }

    std::intptr_t chosen = kInvalid;
    std::string last_error = "connection failed";
    for (addrinfo *ai = result; ai != nullptr; ai = ai->ai_next)
    {
#if defined(_WIN32)
        const SOCKET s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == INVALID_SOCKET)
        {
            last_error = SocketErrorMessage("socket() failed");
            continue;
        }
        const std::intptr_t fd = static_cast<std::intptr_t>(s);
#else
        const int s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s < 0)
        {
            last_error = SocketErrorMessage("socket() failed");
            continue;
        }
        const std::intptr_t fd = static_cast<std::intptr_t>(s);
#if defined(SO_NOSIGPIPE)
        const int one = 1;
        setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, static_cast<socklen_type>(sizeof(one)));
#endif
#endif

        if (!SetNonBlocking(fd, true))
        {
#if defined(_WIN32)
            closesocket(static_cast<SOCKET>(fd));
#else
            ::close(static_cast<int>(fd));
#endif
            last_error = "failed to set non-blocking mode";
            continue;
        }

        const int crc = ::connect(
#if defined(_WIN32)
            static_cast<SOCKET>(fd),
#else
            static_cast<int>(fd),
#endif
            ai->ai_addr, static_cast<socklen_type>(ai->ai_addrlen));

        if (crc == 0)
        {
            chosen = fd;
            break;
        }

        const int err = LastSocketError();
        if (!WouldBlock(err)
#if defined(_WIN32)
            && err != WSAEALREADY
#else
            && err != EINPROGRESS
#endif
        )
        {
#if defined(_WIN32)
            closesocket(static_cast<SOCKET>(fd));
#else
            ::close(static_cast<int>(fd));
#endif
            last_error = SocketErrorMessage("connect() failed");
            continue;
        }

        auto wait = WaitReady(fd, WaitMode::Write, timeout);
        if (!wait)
        {
#if defined(_WIN32)
            closesocket(static_cast<SOCKET>(fd));
#else
            ::close(static_cast<int>(fd));
#endif
            last_error = wait.error().message().data() != nullptr
                             ? std::string{wait.error().message()}
                             : "connect wait failed";
            if (wait.error().code() == ErrorCode::Timeout)
            {
                freeaddrinfo(result);
                return Result<TcpSocket>::Err(Error{ErrorCode::Timeout, "connect timed out"});
            }
            continue;
        }

        int so_error = 0;
        socklen_type len = static_cast<socklen_type>(sizeof(so_error));
#if defined(_WIN32)
        const int gerr = getsockopt(static_cast<SOCKET>(fd), SOL_SOCKET, SO_ERROR,
                                    reinterpret_cast<char *>(&so_error), &len);
#else
        const int gerr = getsockopt(static_cast<int>(fd), SOL_SOCKET, SO_ERROR, &so_error, &len);
#endif
        if (gerr != 0 || so_error != 0)
        {
#if defined(_WIN32)
            closesocket(static_cast<SOCKET>(fd));
#else
            ::close(static_cast<int>(fd));
#endif
            last_error = "connect failed after wait";
            continue;
        }

        chosen = fd;
        break;
    }

    freeaddrinfo(result);

    if (chosen == kInvalid)
    {
        return Result<TcpSocket>::Err(Error{ErrorCode::ConnectFailed, last_error});
    }

    return Result<TcpSocket>::Ok(TcpSocket{chosen});
}

Result<std::size_t> TcpSocket::SendAll(const void *data, std::size_t len,
                                       std::chrono::milliseconds timeout)
{
    if (!valid())
    {
        return Result<std::size_t>::Err(Error{ErrorCode::IoError, "socket closed"});
    }
    const auto *bytes = static_cast<const unsigned char *>(data);
    std::size_t sent_total = 0;
    while (sent_total < len)
    {
        auto wait = WaitReady(fd_, WaitMode::Write, timeout);
        if (!wait)
        {
            return Result<std::size_t>::Err(wait.error());
        }
#if defined(_WIN32)
        const int n =
            ::send(static_cast<SOCKET>(fd_), reinterpret_cast<const char *>(bytes + sent_total),
                   static_cast<int>(len - sent_total), 0);
#else
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
        const ssize_t n =
            ::send(static_cast<int>(fd_), bytes + sent_total, len - sent_total, MSG_NOSIGNAL);
#endif
        if (n < 0)
        {
            const int err = LastSocketError();
            if (WouldBlock(err))
            {
                continue;
            }
            return Result<std::size_t>::Err(
                Error{ErrorCode::IoError, SocketErrorMessage("send failed")});
        }
        if (n == 0)
        {
            return Result<std::size_t>::Err(Error{ErrorCode::IoError, "send returned 0"});
        }
        sent_total += static_cast<std::size_t>(n);
    }
    return Result<std::size_t>::Ok(sent_total);
}

Result<std::size_t> TcpSocket::RecvSome(void *data, std::size_t len,
                                        std::chrono::milliseconds timeout)
{
    if (!valid())
    {
        return Result<std::size_t>::Err(Error{ErrorCode::IoError, "socket closed"});
    }
    auto wait = WaitReady(fd_, WaitMode::Read, timeout);
    if (!wait)
    {
        return Result<std::size_t>::Err(wait.error());
    }
#if defined(_WIN32)
    const int n =
        ::recv(static_cast<SOCKET>(fd_), static_cast<char *>(data), static_cast<int>(len), 0);
#else
    const ssize_t n = ::recv(static_cast<int>(fd_), data, len, 0);
#endif
    if (n < 0)
    {
        const int err = LastSocketError();
        if (WouldBlock(err))
        {
            return Result<std::size_t>::Ok(0);
        }
        return Result<std::size_t>::Err(
            Error{ErrorCode::IoError, SocketErrorMessage("recv failed")});
    }
    return Result<std::size_t>::Ok(static_cast<std::size_t>(n));
}

} // namespace mog::detail
