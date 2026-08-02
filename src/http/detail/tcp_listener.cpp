/**
 * @file tcp_listener.cpp
 * @brief Cross-platform TCP listening socket.
 */
#include "http/detail/tcp_listener.hpp"

#include <cstring>
#include <string>

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
    return err == WSAEWOULDBLOCK;
}
#else
constexpr std::intptr_t kInvalid = -1;

int LastSocketError()
{
    return errno;
}

bool WouldBlock(int err)
{
    return err == EWOULDBLOCK || err == EAGAIN;
}
#endif

void CloseFd(std::intptr_t fd)
{
    if (fd == kInvalid)
    {
        return;
    }
#if defined(_WIN32)
    closesocket(static_cast<SOCKET>(fd));
#else
    ::close(static_cast<int>(fd));
#endif
}

std::string ErrorMessage(const char *prefix)
{
    const int err = LastSocketError();
#if defined(_WIN32)
    return std::string(prefix) + " (code " + std::to_string(err) + ")";
#else
    return std::string(prefix) + ": " + std::strerror(err);
#endif
}

// Read the port a socket is actually bound to (for port 0 / ephemeral binds).
std::uint16_t BoundPort(std::intptr_t fd)
{
    sockaddr_storage addr{};
    socklen_type len = static_cast<socklen_type>(sizeof(addr));
#if defined(_WIN32)
    if (getsockname(static_cast<SOCKET>(fd), reinterpret_cast<sockaddr *>(&addr), &len) != 0)
#else
    if (getsockname(static_cast<int>(fd), reinterpret_cast<sockaddr *>(&addr), &len) != 0)
#endif
    {
        return 0;
    }
    if (addr.ss_family == AF_INET6)
    {
        return ntohs(reinterpret_cast<sockaddr_in6 *>(&addr)->sin6_port);
    }
    return ntohs(reinterpret_cast<sockaddr_in *>(&addr)->sin_port);
}

std::string FormatPeer(const sockaddr_storage &addr)
{
    char buf[INET6_ADDRSTRLEN] = {0};
    if (addr.ss_family == AF_INET6)
    {
        const auto *a = reinterpret_cast<const sockaddr_in6 *>(&addr);
        if (inet_ntop(AF_INET6, &a->sin6_addr, buf, sizeof(buf)) != nullptr)
        {
            return buf;
        }
    }
    else if (addr.ss_family == AF_INET)
    {
        const auto *a = reinterpret_cast<const sockaddr_in *>(&addr);
        if (inet_ntop(AF_INET, &a->sin_addr, buf, sizeof(buf)) != nullptr)
        {
            return buf;
        }
    }
    return {};
}

bool SetNonBlocking(std::intptr_t fd)
{
#if defined(_WIN32)
    u_long mode = 1;
    return ioctlsocket(static_cast<SOCKET>(fd), FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(static_cast<int>(fd), F_GETFL, 0);
    return flags >= 0 && fcntl(static_cast<int>(fd), F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

Result<void> WaitReadable(std::intptr_t fd, std::chrono::milliseconds timeout)
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
    const int rc = select(0, &set, nullptr, nullptr, &tv);
#else
    pollfd pfd{};
    pfd.fd = static_cast<int>(fd);
    pfd.events = POLLIN;
    const int rc = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
#endif
    if (rc == 0)
    {
        return Result<void>::Err(Error{ErrorCode::Timeout, "accept timed out"});
    }
    if (rc < 0)
    {
        return Result<void>::Err(Error{ErrorCode::IoError, ErrorMessage("poll failed")});
    }
    return Result<void>::Ok();
}

} // namespace

TcpListener::~TcpListener()
{
    Close();
}

TcpListener::TcpListener(TcpListener &&other) noexcept : fd_(other.fd_), port_(other.port_)
{
    other.fd_ = kInvalid;
    other.port_ = 0;
}

TcpListener &TcpListener::operator=(TcpListener &&other) noexcept
{
    if (this != &other)
    {
        Close();
        fd_ = other.fd_;
        port_ = other.port_;
        other.fd_ = kInvalid;
        other.port_ = 0;
    }
    return *this;
}

bool TcpListener::valid() const noexcept
{
    return fd_ != kInvalid;
}

void TcpListener::Close() noexcept
{
    if (fd_ != kInvalid)
    {
        CloseFd(fd_);
        fd_ = kInvalid;
    }
}

Result<TcpListener> TcpListener::Bind(std::string_view address, std::uint16_t port, int backlog)
{
    EnsureSocketSubsystem();

    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    const std::string host_str{address};
    const std::string port_str = std::to_string(port);
    addrinfo *result = nullptr;
    const int gai = ::getaddrinfo(host_str.empty() ? nullptr : host_str.c_str(), port_str.c_str(),
                                  &hints, &result);
    if (gai != 0)
    {
        return Result<TcpListener>::Err(
            Error{ErrorCode::InvalidArgument, "cannot resolve bind address: " + host_str});
    }

    std::intptr_t chosen = kInvalid;
    std::string last_error = "bind failed";
    for (addrinfo *ai = result; ai != nullptr; ai = ai->ai_next)
    {
#if defined(_WIN32)
        const SOCKET s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == INVALID_SOCKET)
        {
            last_error = ErrorMessage("socket() failed");
            continue;
        }
        const std::intptr_t fd = static_cast<std::intptr_t>(s);
#else
        const int s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s < 0)
        {
            last_error = ErrorMessage("socket() failed");
            continue;
        }
        const std::intptr_t fd = static_cast<std::intptr_t>(s);
#endif

        // Reuse the address so a quick restart is not blocked by TIME_WAIT.
        const int yes = 1;
#if defined(_WIN32)
        setsockopt(static_cast<SOCKET>(fd), SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char *>(&yes), static_cast<socklen_type>(sizeof(yes)));
#else
        setsockopt(static_cast<int>(fd), SOL_SOCKET, SO_REUSEADDR, &yes,
                   static_cast<socklen_type>(sizeof(yes)));
#endif

        const int brc = ::bind(
#if defined(_WIN32)
            static_cast<SOCKET>(fd),
#else
            static_cast<int>(fd),
#endif
            ai->ai_addr, static_cast<socklen_type>(ai->ai_addrlen));
        if (brc != 0)
        {
            last_error = ErrorMessage("bind() failed");
            CloseFd(fd);
            continue;
        }

        const int lrc = ::listen(
#if defined(_WIN32)
            static_cast<SOCKET>(fd),
#else
            static_cast<int>(fd),
#endif
            backlog);
        if (lrc != 0)
        {
            last_error = ErrorMessage("listen() failed");
            CloseFd(fd);
            continue;
        }

        if (!SetNonBlocking(fd))
        {
            last_error = "failed to set non-blocking mode";
            CloseFd(fd);
            continue;
        }

        chosen = fd;
        break;
    }

    freeaddrinfo(result);

    if (chosen == kInvalid)
    {
        return Result<TcpListener>::Err(Error{ErrorCode::ConnectFailed, last_error});
    }

    return Result<TcpListener>::Ok(TcpListener{chosen, BoundPort(chosen)});
}

Result<TcpSocket> TcpListener::Accept(std::chrono::milliseconds timeout, std::string *peer_address)
{
    if (!valid())
    {
        return Result<TcpSocket>::Err(Error{ErrorCode::IoError, "listener closed"});
    }

    auto ready = WaitReadable(fd_, timeout);
    if (!ready)
    {
        return Result<TcpSocket>::Err(ready.error());
    }

    sockaddr_storage peer{};
    socklen_type len = static_cast<socklen_type>(sizeof(peer));
#if defined(_WIN32)
    const SOCKET s = ::accept(static_cast<SOCKET>(fd_), reinterpret_cast<sockaddr *>(&peer), &len);
    if (s == INVALID_SOCKET)
    {
        const int err = LastSocketError();
        if (WouldBlock(err))
        {
            return Result<TcpSocket>::Err(Error{ErrorCode::Timeout, "no pending connection"});
        }
        return Result<TcpSocket>::Err(Error{ErrorCode::IoError, ErrorMessage("accept failed")});
    }
    const std::intptr_t fd = static_cast<std::intptr_t>(s);
#else
    const int s = ::accept(static_cast<int>(fd_), reinterpret_cast<sockaddr *>(&peer), &len);
    if (s < 0)
    {
        const int err = LastSocketError();
        if (WouldBlock(err))
        {
            return Result<TcpSocket>::Err(Error{ErrorCode::Timeout, "no pending connection"});
        }
        return Result<TcpSocket>::Err(Error{ErrorCode::IoError, ErrorMessage("accept failed")});
    }
    const std::intptr_t fd = static_cast<std::intptr_t>(s);
#endif

    if (peer_address != nullptr)
    {
        *peer_address = FormatPeer(peer);
    }

    return TcpSocket::Adopt(fd);
}

} // namespace mog::detail
