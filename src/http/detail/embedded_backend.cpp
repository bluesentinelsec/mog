/**
 * @file embedded_backend.cpp
 * @brief HTTP/1.1 client over TCP/TLS (default backend) for web client workloads.
 */

#include "http/detail/embedded_backend.hpp"

#include "http/detail/connection_pool.hpp"
#include "http/detail/content_encoding.hpp"
#include "http/detail/env.hpp"
#include "http/detail/prepare.hpp"
#include "http/detail/stream.hpp"
#include "http/detail/url.hpp"
#include "mog/log.hpp"
#include "mog/util.hpp"
#include "mog/version.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace mog::detail
{
namespace
{

bool EqualsIgnoreCase(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
        {
            return false;
        }
    }
    return true;
}

std::string DefaultUserAgent()
{
    return std::string("mog/") + std::string{mog::Version()};
}

std::optional<std::string> ResolveCaBundle(const Options &options)
{
    if (options.ca_bundle.has_value())
    {
        return options.ca_bundle;
    }
    if (auto env = GetEnv("SSL_CERT_FILE"); env.has_value())
    {
        return env;
    }
    if (auto env = GetEnv("REQUESTS_CA_BUNDLE"); env.has_value())
    {
        return env;
    }
    if (auto env = GetEnv("CURL_CA_BUNDLE"); env.has_value())
    {
        return env;
    }
    return std::nullopt;
}

std::string HostHeaderValue(const Url &url)
{
    if (IsDefaultPort(url))
    {
        return url.host;
    }
    return url.host + ":" + std::to_string(url.port);
}

std::string BuildRequestMessage(Method method, const Url &url,
                                const std::map<std::string, std::string> &headers,
                                const std::string &body, bool absolute_form, bool keep_alive)
{
    std::ostringstream oss;
    std::string target;
    if (absolute_form)
    {
        target = BuildUrl(url);
    }
    else
    {
        target = url.path.empty() ? "/" : url.path;
        if (!url.query.empty())
        {
            target.push_back('?');
            target += url.query;
        }
    }

    oss << ToString(method) << ' ' << target << " HTTP/1.1\r\n";

    bool have_host = false;
    bool have_ua = false;
    bool have_conn = false;
    bool have_cl = false;
    bool have_accept = false;

    for (const auto &h : headers)
    {
        if (EqualsIgnoreCase(h.first, "Host"))
        {
            have_host = true;
        }
        if (EqualsIgnoreCase(h.first, "User-Agent"))
        {
            have_ua = true;
        }
        if (EqualsIgnoreCase(h.first, "Connection"))
        {
            have_conn = true;
        }
        if (EqualsIgnoreCase(h.first, "Content-Length"))
        {
            have_cl = true;
        }
        if (EqualsIgnoreCase(h.first, "Accept"))
        {
            have_accept = true;
        }
        oss << h.first << ": " << h.second << "\r\n";
    }

    if (!have_host)
    {
        oss << "Host: " << HostHeaderValue(url) << "\r\n";
    }
    if (!have_ua)
    {
        oss << "User-Agent: " << DefaultUserAgent() << "\r\n";
    }
    if (!have_accept)
    {
        oss << "Accept: */*\r\n";
    }
    if (!have_conn)
    {
        oss << (keep_alive ? "Connection: keep-alive\r\n" : "Connection: close\r\n");
    }
    if (!body.empty() && !have_cl)
    {
        oss << "Content-Length: " << body.size() << "\r\n";
    }
    else if (body.empty() &&
             (method == Method::Post || method == Method::Put || method == Method::Patch) &&
             !have_cl)
    {
        oss << "Content-Length: 0\r\n";
    }
    oss << "\r\n";
    oss << body;
    return oss.str();
}

struct RawResponse
{
    int status = 0;
    std::string reason;
    std::string version; ///< e.g. "HTTP/1.1" from the status line.
    std::vector<Header> headers;
    std::string body;
};

Result<std::string> ReadUntil(Stream &stream, std::string &buffer, std::string_view delim,
                              std::chrono::milliseconds timeout, std::size_t max_bytes)
{
    for (;;)
    {
        const auto pos = buffer.find(delim);
        if (pos != std::string::npos)
        {
            std::string out = buffer.substr(0, pos);
            buffer.erase(0, pos + delim.size());
            return Result<std::string>::Ok(std::move(out));
        }
        if (buffer.size() > max_bytes)
        {
            return Result<std::string>::Err(
                Error{ErrorCode::ProtocolError, "response headers too large"});
        }
        std::array<char, 4096> tmp{};
        auto n = stream.ReadSome(tmp.data(), tmp.size(), timeout);
        if (!n)
        {
            return Result<std::string>::Err(n.error());
        }
        if (*n == 0)
        {
            return Result<std::string>::Err(
                Error{ErrorCode::ProtocolError, "connection closed before headers completed"});
        }
        buffer.append(tmp.data(), *n);
    }
}

Result<void> ReadExact(Stream &stream, std::string &buffer, std::string &out, std::size_t need,
                       std::chrono::milliseconds timeout, std::size_t max_total)
{
    out.clear();
    out.reserve(need);
    while (out.size() < need)
    {
        if (max_total > 0 && out.size() >= max_total)
        {
            return Result<void>::Err(
                Error{ErrorCode::ResponseTooLarge, "response exceeds max_response_bytes"});
        }
        if (!buffer.empty())
        {
            const std::size_t take = std::min(buffer.size(), need - out.size());
            if (max_total > 0 && out.size() + take > max_total)
            {
                return Result<void>::Err(
                    Error{ErrorCode::ResponseTooLarge, "response exceeds max_response_bytes"});
            }
            out.append(buffer.data(), take);
            buffer.erase(0, take);
            continue;
        }
        std::array<char, 8192> tmp{};
        auto n = stream.ReadSome(tmp.data(), tmp.size(), timeout);
        if (!n)
        {
            return Result<void>::Err(n.error());
        }
        if (*n == 0)
        {
            return Result<void>::Err(Error{ErrorCode::ProtocolError, "unexpected EOF in body"});
        }
        buffer.append(tmp.data(), *n);
    }
    return Result<void>::Ok();
}

Result<void> AppendCapped(std::string &body, const char *data, std::size_t n, std::size_t max_total)
{
    if (max_total > 0 && body.size() + n > max_total)
    {
        return Result<void>::Err(
            Error{ErrorCode::ResponseTooLarge, "response exceeds max_response_bytes"});
    }
    body.append(data, n);
    return Result<void>::Ok();
}

Result<RawResponse> ReadHttpResponse(Stream &stream, bool head_request,
                                     std::chrono::milliseconds timeout, std::size_t max_body)
{
    std::string buffer;
    auto header_block = ReadUntil(stream, buffer, "\r\n\r\n", timeout, 1024 * 1024);
    if (!header_block)
    {
        return Result<RawResponse>::Err(header_block.error());
    }

    std::istringstream iss(*header_block);
    std::string status_line;
    if (!std::getline(iss, status_line))
    {
        return Result<RawResponse>::Err(Error{ErrorCode::ProtocolError, "missing status line"});
    }
    if (!status_line.empty() && status_line.back() == '\r')
    {
        status_line.pop_back();
    }

    if (status_line.rfind("HTTP/", 0) != 0)
    {
        return Result<RawResponse>::Err(Error{ErrorCode::ProtocolError, "invalid status line"});
    }
    const auto first_sp = status_line.find(' ');
    if (first_sp == std::string::npos)
    {
        return Result<RawResponse>::Err(Error{ErrorCode::ProtocolError, "invalid status line"});
    }
    const auto second_sp = status_line.find(' ', first_sp + 1);
    std::string code_str = (second_sp == std::string::npos)
                               ? status_line.substr(first_sp + 1)
                               : status_line.substr(first_sp + 1, second_sp - first_sp - 1);
    int status = 0;
    {
        const auto [ptr, ec] =
            std::from_chars(code_str.data(), code_str.data() + code_str.size(), status);
        if (ec != std::errc{} || ptr != code_str.data() + code_str.size())
        {
            return Result<RawResponse>::Err(Error{ErrorCode::ProtocolError, "invalid status code"});
        }
    }

    RawResponse resp;
    resp.status = status;
    resp.version = status_line.substr(0, first_sp);
    if (second_sp != std::string::npos)
    {
        resp.reason = status_line.substr(second_sp + 1);
    }

    std::string line;
    while (std::getline(iss, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (line.empty())
        {
            continue;
        }
        const auto colon = line.find(':');
        if (colon == std::string::npos)
        {
            continue;
        }
        std::string name = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        {
            value.erase(value.begin());
        }
        resp.headers.push_back(Header{std::move(name), std::move(value)});
    }

    if (head_request || status == 204 || status == 304)
    {
        return Result<RawResponse>::Ok(std::move(resp));
    }

    bool chunked = false;
    std::optional<std::size_t> content_length;
    for (const auto &h : resp.headers)
    {
        if (EqualsIgnoreCase(h.name, "Transfer-Encoding"))
        {
            std::string v = h.value;
            for (char &c : v)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (v.find("chunked") != std::string::npos)
            {
                chunked = true;
            }
        }
        if (EqualsIgnoreCase(h.name, "Content-Length"))
        {
            std::size_t cl = 0;
            const auto [ptr, ec] =
                std::from_chars(h.value.data(), h.value.data() + h.value.size(), cl);
            if (ec == std::errc{} && ptr == h.value.data() + h.value.size())
            {
                content_length = cl;
            }
        }
    }

    if (chunked)
    {
        std::string body;
        for (;;)
        {
            auto size_line = ReadUntil(stream, buffer, "\r\n", timeout, 64);
            if (!size_line)
            {
                return Result<RawResponse>::Err(size_line.error());
            }
            std::string hex = *size_line;
            const auto semi = hex.find(';');
            if (semi != std::string::npos)
            {
                hex = hex.substr(0, semi);
            }
            std::size_t chunk_size = 0;
            const auto [ptr, ec] =
                std::from_chars(hex.data(), hex.data() + hex.size(), chunk_size, 16);
            if (ec != std::errc{} || ptr != hex.data() + hex.size())
            {
                return Result<RawResponse>::Err(
                    Error{ErrorCode::ProtocolError, "invalid chunk size"});
            }
            if (chunk_size == 0)
            {
                auto trailers = ReadUntil(stream, buffer, "\r\n", timeout, 64 * 1024);
                if (!trailers)
                {
                    return Result<RawResponse>::Err(trailers.error());
                }
                while (!trailers->empty())
                {
                    auto next = ReadUntil(stream, buffer, "\r\n", timeout, 64 * 1024);
                    if (!next)
                    {
                        return Result<RawResponse>::Err(next.error());
                    }
                    if (next->empty())
                    {
                        break;
                    }
                }
                break;
            }
            if (max_body > 0 && body.size() + chunk_size > max_body)
            {
                return Result<RawResponse>::Err(
                    Error{ErrorCode::ResponseTooLarge, "response exceeds max_response_bytes"});
            }
            std::string chunk;
            auto exact = ReadExact(stream, buffer, chunk, chunk_size, timeout, 0);
            if (!exact)
            {
                return Result<RawResponse>::Err(exact.error());
            }
            body += chunk;
            std::string crlf;
            auto cr = ReadExact(stream, buffer, crlf, 2, timeout, 0);
            if (!cr)
            {
                return Result<RawResponse>::Err(cr.error());
            }
            if (crlf != "\r\n")
            {
                return Result<RawResponse>::Err(
                    Error{ErrorCode::ProtocolError, "missing chunk CRLF"});
            }
        }
        resp.body = std::move(body);
        return Result<RawResponse>::Ok(std::move(resp));
    }

    if (content_length.has_value())
    {
        if (max_body > 0 && *content_length > max_body)
        {
            return Result<RawResponse>::Err(
                Error{ErrorCode::ResponseTooLarge, "Content-Length exceeds max_response_bytes"});
        }
        std::string body;
        auto exact = ReadExact(stream, buffer, body, *content_length, timeout, max_body);
        if (!exact)
        {
            return Result<RawResponse>::Err(exact.error());
        }
        resp.body = std::move(body);
        return Result<RawResponse>::Ok(std::move(resp));
    }

    std::string body = std::move(buffer);
    if (max_body > 0 && body.size() > max_body)
    {
        return Result<RawResponse>::Err(
            Error{ErrorCode::ResponseTooLarge, "response exceeds max_response_bytes"});
    }
    for (;;)
    {
        std::array<char, 8192> tmp{};
        auto n = stream.ReadSome(tmp.data(), tmp.size(), timeout);
        if (!n)
        {
            return Result<RawResponse>::Err(n.error());
        }
        if (*n == 0)
        {
            break;
        }
        auto cap = AppendCapped(body, tmp.data(), *n, max_body);
        if (!cap)
        {
            return Result<RawResponse>::Err(cap.error());
        }
    }
    resp.body = std::move(body);
    return Result<RawResponse>::Ok(std::move(resp));
}

bool IsRedirect(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

Method MethodAfterRedirect(Method method, int status)
{
    if (status == 303)
    {
        return Method::Get;
    }
    if ((status == 301 || status == 302) && method == Method::Post)
    {
        return Method::Get;
    }
    return method;
}

std::map<std::string, std::string> CollectCookies(const std::vector<Header> &headers)
{
    std::map<std::string, std::string> cookies;
    for (const auto &h : headers)
    {
        if (!EqualsIgnoreCase(h.name, "Set-Cookie"))
        {
            continue;
        }
        std::string name;
        std::string value;
        if (ParseSetCookie(h.value, name, value))
        {
            cookies[std::move(name)] = std::move(value);
        }
    }
    return cookies;
}

Result<void> HttpConnectTunnel(Stream &stream, const Url &target, std::chrono::milliseconds timeout)
{
    std::ostringstream oss;
    oss << "CONNECT " << target.host << ':' << target.port << " HTTP/1.1\r\n";
    oss << "Host: " << target.host << ':' << target.port << "\r\n";
    oss << "Connection: keep-alive\r\n\r\n";
    auto wr = stream.WriteString(oss.str(), timeout);
    if (!wr)
    {
        return Result<void>::Err(wr.error());
    }
    // Read proxy response headers only (body empty for 200).
    std::string buffer;
    auto header_block = ReadUntil(stream, buffer, "\r\n\r\n", timeout, 64 * 1024);
    if (!header_block)
    {
        return Result<void>::Err(
            Error{ErrorCode::ProxyError, std::string("proxy CONNECT failed: ") +
                                             std::string{header_block.error().message()}});
    }
    // leftover buffer discarded — CONNECT has no body.
    if (header_block->rfind("HTTP/", 0) != 0)
    {
        return Result<void>::Err(Error{ErrorCode::ProxyError, "invalid proxy CONNECT response"});
    }
    const auto sp = header_block->find(' ');
    if (sp == std::string::npos)
    {
        return Result<void>::Err(Error{ErrorCode::ProxyError, "invalid proxy CONNECT status"});
    }
    int status = 0;
    const auto rest = header_block->substr(sp + 1);
    const auto end = rest.find(' ');
    const std::string code = end == std::string::npos ? rest : rest.substr(0, end);
    const auto [ptr, ec] = std::from_chars(code.data(), code.data() + code.size(), status);
    if (ec != std::errc{} || status < 200 || status >= 300)
    {
        return Result<void>::Err(
            Error{ErrorCode::ProxyError, "proxy CONNECT rejected with status " + code});
    }
    return Result<void>::Ok();
}

Result<std::unique_ptr<Stream>> OpenStream(const Url &url, const Options &options,
                                           std::chrono::milliseconds connect_timeout,
                                           std::chrono::milliseconds io_timeout)
{
    const bool use_proxy = options.proxy.has_value() && !options.proxy->empty();

    if (!use_proxy)
    {
        MOG_LOG_DEBUG("connect {}:{} (timeout={}ms, scheme={})", url.host, url.port,
                      connect_timeout.count(), url.scheme);
        auto sock_result = TcpSocket::Connect(url.host, url.port, connect_timeout);
        if (!sock_result)
        {
            MOG_LOG_WARN("connect failed: {}", sock_result.error().to_string());
            return Result<std::unique_ptr<Stream>>::Err(sock_result.error());
        }
        TcpSocket sock = std::move(*sock_result);
        if (url.scheme == "https")
        {
            MOG_LOG_DEBUG("TLS handshake host={} verify={}", url.host, options.verify_tls);
            TlsSession tls;
            auto hs = tls.Handshake(sock, url.host, options.verify_tls, ResolveCaBundle(options),
                                    io_timeout);
            if (!hs)
            {
                MOG_LOG_WARN("TLS handshake failed: {}", hs.error().to_string());
                return Result<std::unique_ptr<Stream>>::Err(hs.error());
            }
            MOG_LOG_DEBUG("TLS handshake ok");
            return Result<std::unique_ptr<Stream>>::Ok(
                std::make_unique<Stream>(std::move(sock), std::move(tls)));
        }
        return Result<std::unique_ptr<Stream>>::Ok(std::make_unique<Stream>(std::move(sock)));
    }

    auto proxy_url = ParseUrl(*options.proxy);
    if (!proxy_url)
    {
        return Result<std::unique_ptr<Stream>>::Err(
            Error{ErrorCode::ProxyError, "invalid proxy URL: " + *options.proxy});
    }
    if (proxy_url->scheme != "http")
    {
        return Result<std::unique_ptr<Stream>>::Err(
            Error{ErrorCode::ProxyError,
                  "only http:// proxies are supported (got " + proxy_url->scheme + ")"});
    }

    MOG_LOG_DEBUG("proxy connect {}:{} for origin {}:{}", proxy_url->host, proxy_url->port,
                  url.host, url.port);
    auto sock_result = TcpSocket::Connect(proxy_url->host, proxy_url->port, connect_timeout);
    if (!sock_result)
    {
        MOG_LOG_WARN("proxy connect failed: {}", sock_result.error().to_string());
        return Result<std::unique_ptr<Stream>>::Err(sock_result.error());
    }
    TcpSocket sock = std::move(*sock_result);

    if (url.scheme == "https")
    {
        // CONNECT over plain TCP, then TLS handshake on the same socket.
        {
            Stream plain{std::move(sock)};
            MOG_LOG_DEBUG("proxy CONNECT {}:{}", url.host, url.port);
            auto tunnel = HttpConnectTunnel(plain, url, io_timeout);
            if (!tunnel)
            {
                MOG_LOG_WARN("proxy CONNECT failed: {}", tunnel.error().to_string());
                return Result<std::unique_ptr<Stream>>::Err(tunnel.error());
            }
            sock = plain.ReleaseSocket();
        }
        MOG_LOG_DEBUG("TLS handshake via proxy host={} verify={}", url.host, options.verify_tls);
        TlsSession tls;
        auto hs =
            tls.Handshake(sock, url.host, options.verify_tls, ResolveCaBundle(options), io_timeout);
        if (!hs)
        {
            MOG_LOG_WARN("TLS handshake failed: {}", hs.error().to_string());
            return Result<std::unique_ptr<Stream>>::Err(hs.error());
        }
        MOG_LOG_DEBUG("TLS handshake ok (via proxy)");
        return Result<std::unique_ptr<Stream>>::Ok(
            std::make_unique<Stream>(std::move(sock), std::move(tls)));
    }

    return Result<std::unique_ptr<Stream>>::Ok(std::make_unique<Stream>(std::move(sock)));
}

} // namespace

Result<Response> EmbeddedRequest(Method method, std::string_view url_text, const Options &options)
{
    const auto started = std::chrono::steady_clock::now();
    const auto connect_timeout = ConnectTimeout(options);
    const auto io_timeout = IoTimeout(options);
    const PreparedRequest prepared = PrepareRequest(options);

    std::string current_url = AppendQuery(url_text, options.params);
    Method current_method = method;
    std::string body = prepared.body;
    std::map<std::string, std::string> headers = prepared.headers;
    int redirects = 0;
    std::vector<std::string> history;

    MOG_LOG_DEBUG("embedded: start {} {} body={}B connect_timeout={}ms io_timeout={}ms",
                  ToString(method), current_url, body.size(), connect_timeout.count(),
                  io_timeout.count());

    for (;;)
    {
        auto parsed = ParseUrl(current_url);
        if (!parsed)
        {
            MOG_LOG_WARN("embedded: invalid url {}: {}", current_url, parsed.error().to_string());
            return Result<Response>::Err(parsed.error());
        }
        const Url url = *parsed;
        const bool use_proxy = options.proxy.has_value() && !options.proxy->empty();
        const bool absolute_form = use_proxy && url.scheme == "http";

        MOG_LOG_DEBUG("embedded: exchange {} {}://{}:{}{}", ToString(current_method), url.scheme,
                      url.host, url.port, url.path);

        const ConnectionKey conn_key = MakeConnectionKey(url, options.proxy);
        ConnectionPool *pool = nullptr;
        if (options.keep_alive && options.connection_pool)
        {
            pool = static_cast<ConnectionPool *>(options.connection_pool.get());
        }

        std::unique_ptr<Stream> stream;
        bool from_pool = false;
        if (pool != nullptr)
        {
            stream = pool->Take(conn_key);
            if (stream)
            {
                from_pool = true;
                MOG_LOG_DEBUG("embedded: reusing keep-alive connection for {}",
                              conn_key.ToString());
            }
        }
        if (!stream)
        {
            auto stream_result = OpenStream(url, options, connect_timeout, io_timeout);
            if (!stream_result)
            {
                return Result<Response>::Err(stream_result.error());
            }
            stream = std::move(*stream_result);
        }

        const std::string message = BuildRequestMessage(current_method, url, headers, body,
                                                        absolute_form, options.keep_alive);
        MOG_LOG_DEBUG("embedded: send request ({} bytes, absolute_form={}, keep_alive={})",
                      message.size(), absolute_form, options.keep_alive);
        auto wr = stream->WriteString(message, io_timeout);
        if (!wr)
        {
            MOG_LOG_WARN("embedded: write failed: {}", wr.error().to_string());
            if (from_pool && pool != nullptr)
            {
                // Stale idle connection — open a fresh one once.
                stream.reset();
                auto stream_result = OpenStream(url, options, connect_timeout, io_timeout);
                if (!stream_result)
                {
                    return Result<Response>::Err(stream_result.error());
                }
                stream = std::move(*stream_result);
                from_pool = false;
                wr = stream->WriteString(message, io_timeout);
                if (!wr)
                {
                    return Result<Response>::Err(wr.error());
                }
            }
            else
            {
                return Result<Response>::Err(wr.error());
            }
        }

        auto raw = ReadHttpResponse(*stream, current_method == Method::Head, io_timeout,
                                    options.max_response_bytes);
        if (!raw)
        {
            MOG_LOG_WARN("embedded: read failed: {}", raw.error().to_string());
            return Result<Response>::Err(raw.error());
        }

        MOG_LOG_DEBUG("embedded: status {} {} (body {} bytes, version={})", raw->status,
                      raw->reason, raw->body.size(), raw->version);

        const bool can_reuse = options.keep_alive && pool != nullptr &&
                               ResponseAllowsKeepAlive(raw->version, raw->headers);

        if (options.allow_redirects && IsRedirect(raw->status))
        {
            std::string location;
            for (const auto &h : raw->headers)
            {
                if (EqualsIgnoreCase(h.name, "Location"))
                {
                    location = h.value;
                    break;
                }
            }
            if (!location.empty())
            {
                if (can_reuse)
                {
                    pool->Put(conn_key, std::move(stream));
                }
                else
                {
                    stream.reset();
                }

                if (redirects >= options.max_redirects)
                {
                    MOG_LOG_WARN("embedded: too many redirects (max={})", options.max_redirects);
                    return Result<Response>::Err(Error{
                        ErrorCode::TooManyRedirects,
                        "exceeded max_redirects (" + std::to_string(options.max_redirects) + ")"});
                }
                current_url = JoinUrl(current_url, location);
                history.push_back(current_url);
                current_method = MethodAfterRedirect(current_method, raw->status);
                MOG_LOG_INFO("embedded: redirect {} → {} (method now {})", raw->status, current_url,
                             ToString(current_method));
                if (current_method == Method::Get || current_method == Method::Head)
                {
                    body.clear();
                    for (auto it = headers.begin(); it != headers.end();)
                    {
                        if (EqualsIgnoreCase(it->first, "Content-Length") ||
                            EqualsIgnoreCase(it->first, "Content-Type") ||
                            EqualsIgnoreCase(it->first, "Transfer-Encoding"))
                        {
                            it = headers.erase(it);
                        }
                        else
                        {
                            ++it;
                        }
                    }
                }
                ++redirects;
                continue;
            }
        }

        if (can_reuse)
        {
            pool->Put(conn_key, std::move(stream));
        }
        else
        {
            stream.reset();
        }

        Response response;
        response.status_code = raw->status;
        response.reason = std::move(raw->reason);
        response.url = current_url;
        response.headers = std::move(raw->headers);
        response.body = std::move(raw->body);
        response.history_len = redirects;
        response.history = std::move(history);
        response.backend = "embedded";
        response.cookies = CollectCookies(response.headers);

        if (options.decompress && !response.body.empty())
        {
            std::string encoding;
            for (const auto &h : response.headers)
            {
                if (EncodingEquals(h.name, "Content-Encoding"))
                {
                    encoding = h.value;
                    break;
                }
            }
            if (!encoding.empty())
            {
                auto decoded = DecodeContentEncoding(std::move(response.body), encoding,
                                                     options.max_response_bytes);
                if (!decoded)
                {
                    MOG_LOG_WARN("embedded: content-encoding decode failed: {}",
                                 decoded.error().to_string());
                    return Result<Response>::Err(decoded.error());
                }
                response.body = std::move(*decoded);
                StripContentCodingHeaders(response.headers);
            }
        }

        response.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
        return Result<Response>::Ok(std::move(response));
    }
}

} // namespace mog::detail
