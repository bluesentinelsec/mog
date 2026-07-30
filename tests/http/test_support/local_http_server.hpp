/**
 * @file local_http_server.hpp
 * @brief In-process HTTP/1.1 server for embedded client conformance tests.
 *
 * No external network. Used by client_test and conformance_test as the
 * behavioral oracle for the default embedded backend.
 */
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using mog_test_sock_t = SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using mog_test_sock_t = int;
constexpr mog_test_sock_t INVALID_SOCKET = -1;
#endif

namespace mog::test
{

/**
 * @brief One captured request from a client.
 */
struct HttpExchange
{
    std::string method;
    std::string target;
    std::map<std::string, std::string> headers;
    std::string body;
};

/**
 * @brief Scripted server response for a path or default handler.
 */
struct HttpResponseSpec
{
    int status = 200;
    std::string reason; ///< Empty → default reason phrase for @ref status.
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
    /// When set, emit a redirect (status should be 3xx; default 302 if not).
    std::optional<std::string> location;
    /// Emit body with Transfer-Encoding: chunked (no Content-Length).
    bool chunked = false;
    /// When true and method is HEAD, still advertise Content-Length of body but send no entity.
    bool honor_head = true;
    /// When true, respond with Connection: keep-alive and accept another request on the socket.
    bool keep_alive = false;
};

/**
 * @brief Minimal loopback HTTP/1.1 server (one accept thread, Connection: close).
 */
class LocalHttpServer
{
  public:
    LocalHttpServer()
    {
#if defined(_WIN32)
        WSADATA data{};
        WSAStartup(MAKEWORD(2, 2), &data);
#endif
        listen_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_ == INVALID_SOCKET)
        {
            throw std::runtime_error("LocalHttpServer: socket() failed");
        }

        int yes = 1;
#if defined(_WIN32)
        setsockopt(listen_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&yes),
                   sizeof(yes));
#else
        setsockopt(listen_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (::bind(listen_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
        {
            throw std::runtime_error("LocalHttpServer: bind() failed");
        }
        if (::listen(listen_, 32) != 0)
        {
            throw std::runtime_error("LocalHttpServer: listen() failed");
        }

        socklen_t len = sizeof(addr);
        if (::getsockname(listen_, reinterpret_cast<sockaddr *>(&addr), &len) != 0)
        {
            throw std::runtime_error("LocalHttpServer: getsockname() failed");
        }
        port_ = ntohs(addr.sin_port);

        running_ = true;
        thread_ = std::thread([this] { AcceptLoop(); });
    }

    ~LocalHttpServer()
    {
        running_ = false;
#if defined(_WIN32)
        closesocket(listen_);
#else
        ::close(listen_);
#endif
        if (thread_.joinable())
        {
            thread_.join();
        }
    }

    LocalHttpServer(const LocalHttpServer &) = delete;
    LocalHttpServer &operator=(const LocalHttpServer &) = delete;

    [[nodiscard]] std::uint16_t port() const
    {
        return port_;
    }

    [[nodiscard]] std::string origin() const
    {
        return "http://127.0.0.1:" + std::to_string(port_);
    }

    void SetResponse(int status, std::string body,
                     std::vector<std::pair<std::string, std::string>> headers = {},
                     bool chunked = false, bool keep_alive = false)
    {
        std::lock_guard lock(mu_);
        default_.status = status;
        default_.body = std::move(body);
        default_.headers = std::move(headers);
        default_.location.reset();
        default_.chunked = chunked;
        default_.keep_alive = keep_alive;
        default_.reason.clear();
        path_rules_.clear();
    }

    /**
     * @brief Enable keep-alive on the default response (and path rules inherit their own flags).
     */
    void SetKeepAlive(bool enabled)
    {
        std::lock_guard lock(mu_);
        default_.keep_alive = enabled;
    }

    [[nodiscard]] std::uint64_t connection_count() const
    {
        return connections_.load();
    }

    void SetRedirect(int status, const std::string &location)
    {
        std::lock_guard lock(mu_);
        default_.status = status;
        default_.body.clear();
        default_.headers.clear();
        default_.location = location;
        default_.chunked = false;
        path_rules_.clear();
    }

    /** @brief Convenience: 302 redirect. */
    void SetRedirect(const std::string &location)
    {
        SetRedirect(302, location);
    }

    void SetPathResponse(std::string path, HttpResponseSpec spec)
    {
        std::lock_guard lock(mu_);
        path_rules_[std::move(path)] = std::move(spec);
    }

    /**
     * @brief Path-specific body or "REDIRECT:/path" (302) — legacy client_test API.
     */
    void SetPathRule(std::string path, std::string body)
    {
        HttpResponseSpec spec;
        if (body.rfind("REDIRECT:", 0) == 0)
        {
            spec.status = 302;
            spec.location = body.substr(9);
            spec.body.clear();
        }
        else
        {
            spec.status = 200;
            spec.body = std::move(body);
        }
        SetPathResponse(std::move(path), std::move(spec));
    }

    void RequireAuth(std::string expected_header_value)
    {
        std::lock_guard lock(mu_);
        require_auth_ = std::move(expected_header_value);
    }

    /**
     * @brief Reply 401 with a Digest challenge until a request carries any
     *        Authorization header (then serve the configured response).
     */
    void RequireDigestAuth(std::string realm = "testrealm", std::string nonce = "server-nonce-123")
    {
        std::lock_guard lock(mu_);
        require_digest_ = true;
        digest_realm_ = std::move(realm);
        digest_nonce_ = std::move(nonce);
    }

    [[nodiscard]] HttpExchange Last() const
    {
        std::lock_guard lock(mu_);
        return last_;
    }

    [[nodiscard]] std::vector<HttpExchange> History() const
    {
        std::lock_guard lock(mu_);
        return history_;
    }

    void ClearHistory()
    {
        std::lock_guard lock(mu_);
        history_.clear();
        last_ = {};
    }

  private:
    static const char *DefaultReason(int status)
    {
        switch (status)
        {
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 204:
            return "No Content";
        case 301:
            return "Moved Permanently";
        case 302:
            return "Found";
        case 303:
            return "See Other";
        case 307:
            return "Temporary Redirect";
        case 308:
            return "Permanent Redirect";
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 500:
            return "Internal Server Error";
        case 502:
            return "Bad Gateway";
        case 503:
            return "Service Unavailable";
        default:
            return "OK";
        }
    }

    static void SetSocketTimeouts(mog_test_sock_t fd, int seconds)
    {
#if defined(_WIN32)
        DWORD ms = static_cast<DWORD>(seconds * 1000);
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&ms), sizeof(ms));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&ms), sizeof(ms));
#else
        timeval tv{};
        tv.tv_sec = seconds;
        tv.tv_usec = 0;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    }

    static std::string FormatChunked(const std::string &body)
    {
        std::ostringstream oss;
        constexpr std::size_t kChunk = 8;
        std::size_t off = 0;
        while (off < body.size())
        {
            // Parenthesize to avoid Windows min/max macros from winsock headers.
            const std::size_t n = (std::min)(kChunk, body.size() - off);
            oss << std::hex << n << "\r\n";
            oss << body.substr(off, n) << "\r\n";
            off += n;
        }
        oss << "0\r\n\r\n";
        return oss.str();
    }

    static std::string BuildWire(const HttpResponseSpec &spec, const std::string &method)
    {
        const char *reason = spec.reason.empty() ? DefaultReason(spec.status) : spec.reason.c_str();
        std::ostringstream oss;
        oss << "HTTP/1.1 " << spec.status << ' ' << reason << "\r\n";

        const char *conn = spec.keep_alive ? "keep-alive" : "close";
        if (spec.location.has_value())
        {
            oss << "Location: " << *spec.location << "\r\n";
            oss << "Content-Length: 0\r\n";
            oss << "Connection: " << conn << "\r\n";
            for (const auto &h : spec.headers)
            {
                oss << h.first << ": " << h.second << "\r\n";
            }
            oss << "\r\n";
            return oss.str();
        }

        const bool omit_body = (spec.honor_head && (method == "HEAD" || method == "head")) ||
                               spec.status == 204 || spec.status == 304;

        if (spec.chunked && !omit_body)
        {
            oss << "Transfer-Encoding: chunked\r\n";
        }
        else
        {
            oss << "Content-Length: " << spec.body.size() << "\r\n";
        }
        oss << "Connection: " << conn << "\r\n";
        for (const auto &h : spec.headers)
        {
            oss << h.first << ": " << h.second << "\r\n";
        }
        oss << "\r\n";
        if (!omit_body)
        {
            if (spec.chunked)
            {
                oss << FormatChunked(spec.body);
            }
            else
            {
                oss << spec.body;
            }
        }
        return oss.str();
    }

    void AcceptLoop()
    {
        SetSocketTimeouts(listen_, 1);
        while (running_)
        {
            mog_test_sock_t client = ::accept(listen_, nullptr, nullptr);
            if (client == INVALID_SOCKET)
            {
                if (!running_)
                {
                    break;
                }
                continue;
            }
            SetSocketTimeouts(client, 5);
            ++connections_;
            HandleClient(client);
#if defined(_WIN32)
            closesocket(client);
#else
            ::close(client);
#endif
        }
    }

    void HandleClient(mog_test_sock_t client)
    {
        std::string pending;
        char buf[4096];
        for (;;)
        {
            std::string req = std::move(pending);
            pending.clear();
            while (req.find("\r\n\r\n") == std::string::npos)
            {
#if defined(_WIN32)
                const int n = ::recv(client, buf, sizeof(buf), 0);
#else
                const ssize_t n = ::recv(client, buf, sizeof(buf), 0);
#endif
                if (n <= 0)
                {
                    return;
                }
                req.append(buf, static_cast<std::size_t>(n));
                if (req.size() > 1024 * 1024)
                {
                    return;
                }
            }

            HttpExchange ex;
            const auto line_end = req.find("\r\n");
            if (line_end == std::string::npos)
            {
                return;
            }
            {
                std::istringstream iss(req.substr(0, line_end));
                iss >> ex.method >> ex.target;
            }
            std::size_t pos = line_end + 2;
            std::size_t content_length = 0;
            while (pos < req.size())
            {
                const auto next = req.find("\r\n", pos);
                if (next == std::string::npos || next == pos)
                {
                    pos = next == std::string::npos ? req.size() : next + 2;
                    break;
                }
                const std::string line = req.substr(pos, next - pos);
                const auto colon = line.find(':');
                if (colon != std::string::npos)
                {
                    std::string name = line.substr(0, colon);
                    std::string value = line.substr(colon + 1);
                    while (!value.empty() && value.front() == ' ')
                    {
                        value.erase(value.begin());
                    }
                    if (name == "Content-Length" || name == "content-length")
                    {
                        content_length = static_cast<std::size_t>(std::stoul(value));
                    }
                    ex.headers[name] = value;
                }
                pos = next + 2;
            }
            const std::size_t body_start = req.find("\r\n\r\n");
            if (body_start != std::string::npos)
            {
                ex.body = req.substr(body_start + 4);
                while (ex.body.size() < content_length)
                {
#if defined(_WIN32)
                    const int n = ::recv(client, buf, sizeof(buf), 0);
#else
                    const ssize_t n = ::recv(client, buf, sizeof(buf), 0);
#endif
                    if (n <= 0)
                    {
                        break;
                    }
                    ex.body.append(buf, static_cast<std::size_t>(n));
                }
                if (ex.body.size() > content_length)
                {
                    pending = ex.body.substr(content_length);
                    ex.body.resize(content_length);
                }
            }

            std::string response;
            bool stay_open = false;
            {
                std::lock_guard lock(mu_);
                last_ = ex;
                history_.push_back(ex);

                if (require_digest_)
                {
                    bool has_auth = false;
                    for (const auto &h : ex.headers)
                    {
                        if (h.first == "Authorization" || h.first == "authorization")
                        {
                            has_auth = true;
                        }
                    }
                    if (!has_auth)
                    {
                        response =
                            "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Digest realm=\"" +
                            digest_realm_ + "\", nonce=\"" + digest_nonce_ +
                            "\", qop=\"auth\"\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                        ::send(client, response.data(),
#if defined(_WIN32)
                               static_cast<int>(response.size()),
#else
                               response.size(),
#endif
                               0);
                        return;
                    }
                }

                if (!require_auth_.empty())
                {
                    bool ok = false;
                    for (const auto &h : ex.headers)
                    {
                        if (h.first == "Authorization" || h.first == "authorization")
                        {
                            if (h.second == require_auth_)
                            {
                                ok = true;
                            }
                        }
                    }
                    if (!ok)
                    {
                        response = "HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n"
                                   "Connection: close\r\n\r\n";
                        ::send(client, response.data(),
#if defined(_WIN32)
                               static_cast<int>(response.size()),
#else
                               response.size(),
#endif
                               0);
                        return;
                    }
                }

                std::string path = ex.target;
                const auto q = path.find('?');
                if (q != std::string::npos)
                {
                    path = path.substr(0, q);
                }

                HttpResponseSpec spec = default_;
                if (auto it = path_rules_.find(path); it != path_rules_.end())
                {
                    spec = it->second;
                }
                // Honor client Connection: close even if server prefers keep-alive.
                for (const auto &h : ex.headers)
                {
                    if ((h.first == "Connection" || h.first == "connection") &&
                        h.second.find("close") != std::string::npos)
                    {
                        spec.keep_alive = false;
                    }
                }
                stay_open = spec.keep_alive;
                response = BuildWire(spec, ex.method);
            }

            ::send(client, response.data(),
#if defined(_WIN32)
                   static_cast<int>(response.size()),
#else
                   response.size(),
#endif
                   0);
            if (!stay_open)
            {
                return;
            }
        }
    }

    mog_test_sock_t listen_{INVALID_SOCKET};
    std::uint16_t port_{0};
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> connections_{0};
    mutable std::mutex mu_;
    HttpExchange last_{};
    std::vector<HttpExchange> history_;
    HttpResponseSpec default_{};
    std::string require_auth_;
    bool require_digest_ = false;
    std::string digest_realm_;
    std::string digest_nonce_;
    std::map<std::string, HttpResponseSpec> path_rules_;
};

} // namespace mog::test
