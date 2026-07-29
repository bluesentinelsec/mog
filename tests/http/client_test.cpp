/**
 * @file client_test.cpp
 * @brief Integration tests against an in-process HTTP/1.1 server.
 */

#include "mog/mog.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <map>
#include <miniz.h>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using sock_t = SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using sock_t = int;
constexpr sock_t INVALID_SOCKET = -1;
#endif

namespace
{

class LocalHttpServer
{
  public:
    struct Exchange
    {
        std::string method;
        std::string target;
        std::map<std::string, std::string> headers;
        std::string body;
    };

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
        if (::listen(listen_, 16) != 0)
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

    [[nodiscard]] std::uint16_t port() const
    {
        return port_;
    }

    [[nodiscard]] std::string origin() const
    {
        return "http://127.0.0.1:" + std::to_string(port_);
    }

    void SetResponse(int status, std::string body,
                     std::vector<std::pair<std::string, std::string>> headers = {})
    {
        std::lock_guard lock(mu_);
        status_ = status;
        body_ = std::move(body);
        extra_headers_ = std::move(headers);
        redirect_to_.clear();
    }

    void SetRedirect(const std::string &location)
    {
        std::lock_guard lock(mu_);
        status_ = 302;
        body_.clear();
        redirect_to_ = location;
        path_rules_.clear();
    }

    /**
     * @brief Path-specific responses: exact target path (no query) → body.
     * Special: if body is the string "REDIRECT:/path", send 302 to that path.
     */
    void SetPathRule(std::string path, std::string body)
    {
        std::lock_guard lock(mu_);
        path_rules_[std::move(path)] = std::move(body);
    }

    void RequireAuth(std::string expected_header_value)
    {
        std::lock_guard lock(mu_);
        require_auth_ = std::move(expected_header_value);
    }

    [[nodiscard]] Exchange Last() const
    {
        std::lock_guard lock(mu_);
        return last_;
    }

  private:
    static void SetSocketTimeouts(sock_t fd, int seconds)
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

    void AcceptLoop()
    {
        // Accept timeout so destructor can join promptly under parallel ctest.
        SetSocketTimeouts(listen_, 1);
        while (running_)
        {
            sock_t client = ::accept(listen_, nullptr, nullptr);
            if (client == INVALID_SOCKET)
            {
                if (!running_)
                {
                    break;
                }
                continue; // timeout — retry while still running
            }
            SetSocketTimeouts(client, 5);
            HandleClient(client);
#if defined(_WIN32)
            closesocket(client);
#else
            ::close(client);
#endif
        }
    }

    void HandleClient(sock_t client)
    {
        std::string req;
        char buf[4096];
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

        Exchange ex;
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
                ex.body.resize(content_length);
            }
        }

        std::string response;
        {
            std::lock_guard lock(mu_);
            last_ = ex;

            if (!require_auth_.empty())
            {
                auto it = ex.headers.find("Authorization");
                if (it == ex.headers.end())
                {
                    // case-insensitive scan
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
                else if (it->second != require_auth_)
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

            if (auto it = path_rules_.find(path); it != path_rules_.end())
            {
                if (it->second.rfind("REDIRECT:", 0) == 0)
                {
                    const std::string loc = it->second.substr(9);
                    response = "HTTP/1.1 302 Found\r\nLocation: " + loc +
                               "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                }
                else
                {
                    std::ostringstream oss;
                    oss << "HTTP/1.1 200 OK\r\nContent-Length: " << it->second.size()
                        << "\r\nConnection: close\r\n\r\n"
                        << it->second;
                    response = oss.str();
                }
            }
            else if (!redirect_to_.empty())
            {
                response = "HTTP/1.1 302 Found\r\nLocation: " + redirect_to_ +
                           "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            }
            else
            {
                std::ostringstream oss;
                oss << "HTTP/1.1 " << status_ << " OK\r\n";
                oss << "Content-Length: " << body_.size() << "\r\n";
                oss << "Connection: close\r\n";
                for (const auto &h : extra_headers_)
                {
                    oss << h.first << ": " << h.second << "\r\n";
                }
                oss << "\r\n" << body_;
                response = oss.str();
            }
        }

        ::send(client, response.data(),
#if defined(_WIN32)
               static_cast<int>(response.size()),
#else
               response.size(),
#endif
               0);
    }

    sock_t listen_{INVALID_SOCKET};
    std::uint16_t port_{0};
    std::thread thread_;
    std::atomic<bool> running_{false};
    mutable std::mutex mu_;
    Exchange last_{};
    int status_{200};
    std::string body_{"ok"};
    std::vector<std::pair<std::string, std::string>> extra_headers_;
    std::string redirect_to_;
    std::string require_auth_;
    std::map<std::string, std::string> path_rules_;
};

} // namespace

std::string MakeGzipBody(std::string_view plain)
{
    mz_stream stream{};
    EXPECT_EQ(mz_deflateInit2(&stream, MZ_DEFAULT_COMPRESSION, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS,
                              8, MZ_DEFAULT_STRATEGY),
              MZ_OK);
    stream.next_in = reinterpret_cast<const unsigned char *>(plain.data());
    stream.avail_in = static_cast<unsigned int>(plain.size());
    std::string deflated(plain.size() + 64, '\0');
    stream.next_out = reinterpret_cast<unsigned char *>(deflated.data());
    stream.avail_out = static_cast<unsigned int>(deflated.size());
    EXPECT_EQ(mz_deflate(&stream, MZ_FINISH), MZ_STREAM_END);
    deflated.resize(static_cast<std::size_t>(stream.total_out));
    mz_deflateEnd(&stream);

    std::string out;
    out.push_back(static_cast<char>(0x1f));
    out.push_back(static_cast<char>(0x8b));
    out.push_back(8);
    out.push_back(0);
    out.append(4, '\0');
    out.push_back(0);
    out.push_back(static_cast<char>(255));
    out.append(deflated);
    const mz_ulong crc =
        mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char *>(plain.data()),
                 static_cast<size_t>(plain.size()));
    const auto isize = static_cast<std::uint32_t>(plain.size());
    for (int i = 0; i < 4; ++i)
    {
        out.push_back(static_cast<char>((crc >> (8 * i)) & 0xffU));
    }
    for (int i = 0; i < 4; ++i)
    {
        out.push_back(static_cast<char>((isize >> (8 * i)) & 0xffU));
    }
    return out;
}

TEST(ClientTest, GetOk)
{
    LocalHttpServer server;
    server.SetResponse(200, "hello world", {{"X-Test", "1"}});

    auto r = mog::get(server.origin() + "/path");
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(r->status_code, 200);
    EXPECT_EQ(r->text(), "hello world");
    EXPECT_EQ(r->header("X-Test"), "1");
    EXPECT_EQ(r->backend, "embedded");
    EXPECT_GE(r->elapsed.count(), 0);
    EXPECT_EQ(server.Last().method, "GET");
    EXPECT_EQ(server.Last().target, "/path");
}

TEST(ClientTest, GzipBodyIsDecoded)
{
    LocalHttpServer server;
    const std::string plain = "hello compressed world";
    server.SetResponse(200, MakeGzipBody(plain),
                       {{"Content-Encoding", "gzip"}, {"Content-Type", "text/plain"}});

    auto r = mog::get(server.origin() + "/gz");
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(r->text(), plain);
    EXPECT_TRUE(r->header("Content-Encoding").empty());
    EXPECT_EQ(r->header("Content-Type"), "text/plain");

    bool saw_accept = false;
    for (const auto &h : server.Last().headers)
    {
        if (h.first == "Accept-Encoding" || h.first == "accept-encoding")
        {
            EXPECT_NE(h.second.find("gzip"), std::string::npos);
            saw_accept = true;
        }
    }
    EXPECT_TRUE(saw_accept);
}

TEST(ClientTest, DecompressCanBeDisabled)
{
    LocalHttpServer server;
    const std::string plain = "raw-bytes";
    const std::string gz = MakeGzipBody(plain);
    server.SetResponse(200, gz, {{"Content-Encoding", "gzip"}});

    mog::Options opt;
    opt.decompress = false;
    auto r = mog::get(server.origin() + "/gz", opt);
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(r->body, gz);
    EXPECT_EQ(r->header("Content-Encoding"), "gzip");
}

TEST(ClientTest, PostJson)
{
    LocalHttpServer server;
    server.SetResponse(201, "{\"ok\":true}");

    mog::Options opt;
    mog::WithJson(opt, R"({"a":1})");
    auto r = mog::post(server.origin() + "/api", opt);
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(r->status_code, 201);
    EXPECT_EQ(server.Last().method, "POST");
    EXPECT_EQ(server.Last().body, R"({"a":1})");
    bool saw_ct = false;
    for (const auto &h : server.Last().headers)
    {
        if (h.first == "Content-Type" || h.first == "content-type")
        {
            EXPECT_NE(h.second.find("application/json"), std::string::npos);
            saw_ct = true;
        }
    }
    EXPECT_TRUE(saw_ct);
}

TEST(ClientTest, PostForm)
{
    LocalHttpServer server;
    server.SetResponse(200, "ok");

    mog::Options opt;
    mog::WithForm(opt, {{"user", "a b"}, {"x", "1"}});
    auto r = mog::post(server.origin() + "/form", opt);
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_NE(server.Last().body.find("user=a+b"), std::string::npos);
    EXPECT_NE(server.Last().body.find("x=1"), std::string::npos);
}

TEST(ClientTest, BasicAuth)
{
    LocalHttpServer server;
    server.SetResponse(200, "secret");
    // "user:pass" base64
    server.RequireAuth("Basic " + mog::Base64Encode("user:pass"));

    mog::Options opt;
    mog::WithBasicAuth(opt, "user", "pass");
    auto r = mog::get(server.origin() + "/secure", opt);
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(r->status_code, 200);
    EXPECT_EQ(r->text(), "secret");
}

TEST(ClientTest, BearerAuth)
{
    LocalHttpServer server;
    server.SetResponse(200, "ok");
    server.RequireAuth("Bearer tok123");

    mog::Options opt;
    mog::WithBearerToken(opt, "tok123");
    auto r = mog::get(server.origin() + "/t", opt);
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(r->status_code, 200);
}

TEST(ClientTest, QueryParams)
{
    LocalHttpServer server;
    server.SetResponse(200, "ok");

    mog::Options opt;
    opt.params = {{"q", "hello world"}, {"n", "1"}};
    auto r = mog::get(server.origin() + "/search", opt);
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_NE(server.Last().target.find("q=hello"), std::string::npos);
    EXPECT_NE(server.Last().target.find("n=1"), std::string::npos);
}

TEST(ClientTest, CookiesRoundTrip)
{
    LocalHttpServer server;
    server.SetResponse(200, "ok", {{"Set-Cookie", "sid=abc; Path=/"}, {"Set-Cookie", "x=1"}});

    // Note: our simple server only sends one of each header name in map - we push vector of pairs
    // but when building response we can send two Set-Cookie lines.
    // SetResponse stores vector - good, two Set-Cookie entries work.

    auto r = mog::get(server.origin() + "/");
    ASSERT_TRUE(r) << r.error().to_string();
    // Server implementation: extra_headers with two Set-Cookie - CollectCookies should find both.
    // Our LocalHttpServer can emit multiple Set-Cookie from the vector.
    EXPECT_FALSE(r->cookies.empty());
}

TEST(ClientTest, SessionCookieJar)
{
    LocalHttpServer server;
    server.SetResponse(200, "ok", {{"Set-Cookie", "session=xyz"}});

    mog::Session s;
    auto r1 = s.get(server.origin() + "/login");
    ASSERT_TRUE(r1) << r1.error().to_string();
    EXPECT_EQ(s.cookies().count("session"), 1U);

    server.SetResponse(200, "ok");
    auto r2 = s.get(server.origin() + "/me");
    ASSERT_TRUE(r2) << r2.error().to_string();
    const auto last = server.Last();
    bool found = false;
    for (const auto &h : last.headers)
    {
        if (h.first == "Cookie" || h.first == "cookie")
        {
            EXPECT_NE(h.second.find("session=xyz"), std::string::npos);
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(ClientTest, RedirectFollow)
{
    LocalHttpServer server;
    server.SetPathRule("/start", "REDIRECT:/final");
    server.SetPathRule("/final", "landed");

    auto r = mog::get(server.origin() + "/start");
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(r->status_code, 200);
    EXPECT_EQ(r->text(), "landed");
    EXPECT_EQ(r->history_len, 1);
    EXPECT_EQ(r->url, server.origin() + "/final");
}

TEST(ClientTest, RaiseForStatus)
{
    LocalHttpServer server;
    server.SetResponse(404, "missing");
    auto r = mog::get(server.origin() + "/nope");
    ASSERT_TRUE(r);
    auto check = r->raise_for_status();
    EXPECT_FALSE(check);
    EXPECT_EQ(check.error().code(), mog::ErrorCode::HttpStatus);
}

TEST(ClientTest, MaxResponseBytes)
{
    LocalHttpServer server;
    server.SetResponse(200, std::string(1000, 'x'));
    mog::Options opt;
    opt.max_response_bytes = 100;
    auto r = mog::get(server.origin() + "/big", opt);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code(), mog::ErrorCode::ResponseTooLarge);
}

TEST(UtilTest, Base64AndForm)
{
    EXPECT_EQ(mog::Base64Encode("user:pass"), "dXNlcjpwYXNz");
    EXPECT_EQ(mog::EncodeForm({{"a", "b c"}}), "a=b+c");
    std::string name;
    std::string value;
    ASSERT_TRUE(mog::ParseSetCookie("sid=abc; Path=/; HttpOnly", name, value));
    EXPECT_EQ(name, "sid");
    EXPECT_EQ(value, "abc");
}
