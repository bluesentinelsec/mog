/**
 * @file embedded_backend.cpp
 * @brief HTTP/1.1 client over TCP/TLS (default backend).
 */

#include "http/detail/embedded_backend.hpp"

#include "http/detail/stream.hpp"
#include "http/detail/url.hpp"
#include "mog/version.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdlib>
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
    if (const char *env = std::getenv("SSL_CERT_FILE"); env != nullptr && env[0] != '\0')
    {
        return std::string{env};
    }
    if (const char *env = std::getenv("REQUESTS_CA_BUNDLE"); env != nullptr && env[0] != '\0')
    {
        return std::string{env};
    }
    if (const char *env = std::getenv("CURL_CA_BUNDLE"); env != nullptr && env[0] != '\0')
    {
        return std::string{env};
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

std::string BuildRequestLineAndHeaders(Method method, const Url &url,
                                       const std::map<std::string, std::string> &headers,
                                       const std::string &body, const std::string &user_agent)
{
    std::ostringstream oss;
    const std::string path = url.path.empty() ? "/" : url.path;
    oss << ToString(method) << ' ' << path;
    if (!url.query.empty())
    {
        oss << '?' << url.query;
    }
    oss << " HTTP/1.1\r\n";

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
        const std::string ua = user_agent.empty() ? DefaultUserAgent() : user_agent;
        oss << "User-Agent: " << ua << "\r\n";
    }
    if (!have_accept)
    {
        oss << "Accept: */*\r\n";
    }
    if (!have_conn)
    {
        oss << "Connection: close\r\n";
    }
    if (!body.empty() && !have_cl)
    {
        oss << "Content-Length: " << body.size() << "\r\n";
    }
    oss << "\r\n";
    return oss.str();
}

struct RawResponse
{
    int status = 0;
    std::string reason;
    std::map<std::string, std::string> headers;
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
                       std::chrono::milliseconds timeout)
{
    out.clear();
    out.reserve(need);
    while (out.size() < need)
    {
        if (!buffer.empty())
        {
            const std::size_t take = std::min(buffer.size(), need - out.size());
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

Result<RawResponse> ReadHttpResponse(Stream &stream, bool head_request,
                                     std::chrono::milliseconds timeout)
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

    // HTTP/1.1 200 OK
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
        // trim value leading space
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        {
            value.erase(value.begin());
        }
        resp.headers.emplace(std::move(name), std::move(value));
    }

    if (head_request || status == 204 || status == 304)
    {
        return Result<RawResponse>::Ok(std::move(resp));
    }

    // Transfer-Encoding: chunked?
    bool chunked = false;
    std::optional<std::size_t> content_length;
    for (const auto &h : resp.headers)
    {
        if (EqualsIgnoreCase(h.first, "Transfer-Encoding"))
        {
            std::string v = h.second;
            for (char &c : v)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (v.find("chunked") != std::string::npos)
            {
                chunked = true;
            }
        }
        if (EqualsIgnoreCase(h.first, "Content-Length"))
        {
            std::size_t cl = 0;
            const auto [ptr, ec] =
                std::from_chars(h.second.data(), h.second.data() + h.second.size(), cl);
            if (ec == std::errc{} && ptr == h.second.data() + h.second.size())
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
            // ignore chunk extensions
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
                // trailer headers until blank line
                auto trailers = ReadUntil(stream, buffer, "\r\n", timeout, 64 * 1024);
                if (!trailers)
                {
                    return Result<RawResponse>::Err(trailers.error());
                }
                // if non-empty trailer start, consume until empty line
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
            std::string chunk;
            auto exact = ReadExact(stream, buffer, chunk, chunk_size, timeout);
            if (!exact)
            {
                return Result<RawResponse>::Err(exact.error());
            }
            body += chunk;
            // trailing CRLF after chunk
            std::string crlf;
            auto cr = ReadExact(stream, buffer, crlf, 2, timeout);
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
        std::string body;
        auto exact = ReadExact(stream, buffer, body, *content_length, timeout);
        if (!exact)
        {
            return Result<RawResponse>::Err(exact.error());
        }
        resp.body = std::move(body);
        return Result<RawResponse>::Ok(std::move(resp));
    }

    // Read until EOF (Connection: close).
    std::string body = std::move(buffer);
    for (;;)
    {
        std::array<char, 8192> tmp{};
        auto n = stream.ReadSome(tmp.data(), tmp.size(), timeout);
        if (!n)
        {
            // timeout with partial body — treat as error
            return Result<RawResponse>::Err(n.error());
        }
        if (*n == 0)
        {
            break;
        }
        body.append(tmp.data(), *n);
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
        // Common browser-like behavior for 301/302 on POST.
        return Method::Get;
    }
    return method;
}

} // namespace

Result<Response> EmbeddedRequest(Method method, std::string_view url_text, const Options &options)
{
    std::string current_url = AppendQuery(url_text, options.params);
    Method current_method = method;
    std::string body = options.body;
    int redirects = 0;

    for (;;)
    {
        auto parsed = ParseUrl(current_url);
        if (!parsed)
        {
            return Result<Response>::Err(parsed.error());
        }
        const Url url = *parsed;

        auto sock_result = TcpSocket::Connect(url.host, url.port, options.timeout);
        if (!sock_result)
        {
            return Result<Response>::Err(sock_result.error());
        }
        TcpSocket sock = std::move(*sock_result);

        std::unique_ptr<Stream> stream;
        if (url.scheme == "https")
        {
            TlsSession tls;
            auto hs = tls.Handshake(sock, url.host, options.verify_tls, ResolveCaBundle(options),
                                    options.timeout);
            if (!hs)
            {
                return Result<Response>::Err(hs.error());
            }
            stream = std::make_unique<Stream>(std::move(sock), std::move(tls));
        }
        else
        {
            stream = std::make_unique<Stream>(std::move(sock));
        }

        const std::string request_head = BuildRequestLineAndHeaders(
            current_method, url, options.headers, body, options.user_agent);
        auto wr = stream->WriteString(request_head, options.timeout);
        if (!wr)
        {
            return Result<Response>::Err(wr.error());
        }
        if (!body.empty() && current_method != Method::Head && current_method != Method::Get)
        {
            auto wb = stream->WriteString(body, options.timeout);
            if (!wb)
            {
                return Result<Response>::Err(wb.error());
            }
        }
        else if (!body.empty() && (current_method == Method::Get || current_method == Method::Head))
        {
            // Still send body if caller provided one (unusual but allowed).
            auto wb = stream->WriteString(body, options.timeout);
            if (!wb)
            {
                return Result<Response>::Err(wb.error());
            }
        }

        auto raw = ReadHttpResponse(*stream, current_method == Method::Head, options.timeout);
        if (!raw)
        {
            return Result<Response>::Err(raw.error());
        }

        if (options.allow_redirects && IsRedirect(raw->status))
        {
            std::string location;
            for (const auto &h : raw->headers)
            {
                if (EqualsIgnoreCase(h.first, "Location"))
                {
                    location = h.second;
                    break;
                }
            }
            if (!location.empty())
            {
                if (redirects >= options.max_redirects)
                {
                    return Result<Response>::Err(Error{
                        ErrorCode::TooManyRedirects,
                        "exceeded max_redirects (" + std::to_string(options.max_redirects) + ")"});
                }
                current_url = JoinUrl(current_url, location);
                current_method = MethodAfterRedirect(current_method, raw->status);
                if (current_method == Method::Get || current_method == Method::Head)
                {
                    body.clear();
                }
                ++redirects;
                continue;
            }
        }

        Response response;
        response.status_code = raw->status;
        response.reason = std::move(raw->reason);
        response.url = current_url;
        response.headers = std::move(raw->headers);
        response.body = std::move(raw->body);
        response.history_len = redirects;
        response.backend = "embedded";
        return Result<Response>::Ok(std::move(response));
    }
}

} // namespace mog::detail
