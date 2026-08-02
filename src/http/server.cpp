/**
 * @file server.cpp
 * @brief Embedded HTTP/1.1 server implementation (thread pool + blocking I/O).
 */
#include "mog/server.hpp"

#include "http/detail/socket.hpp"
#include "http/detail/tcp_listener.hpp"
#include "http/detail/tls_server.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace mog
{
namespace
{

using Clock = std::chrono::steady_clock;

// --- Small string helpers --------------------------------------------------

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

std::string_view TrimWs(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
    {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
    {
        s.remove_suffix(1);
    }
    return s;
}

int HexValue(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

std::string PercentDecode(std::string_view in, bool plus_as_space)
{
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i)
    {
        const char c = in[i];
        if (c == '%' && i + 2 < in.size())
        {
            const int hi = HexValue(in[i + 1]);
            const int lo = HexValue(in[i + 2]);
            if (hi >= 0 && lo >= 0)
            {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        if (plus_as_space && c == '+')
        {
            out.push_back(' ');
            continue;
        }
        out.push_back(c);
    }
    return out;
}

std::map<std::string, std::string> ParseQuery(std::string_view query)
{
    std::map<std::string, std::string> out;
    std::size_t pos = 0;
    while (pos < query.size())
    {
        std::size_t amp = query.find('&', pos);
        std::string_view pair =
            query.substr(pos, amp == std::string_view::npos ? std::string_view::npos : amp - pos);
        if (!pair.empty())
        {
            std::size_t eq = pair.find('=');
            if (eq == std::string_view::npos)
            {
                out[PercentDecode(pair, true)] = std::string{};
            }
            else
            {
                out[PercentDecode(pair.substr(0, eq), true)] =
                    PercentDecode(pair.substr(eq + 1), true);
            }
        }
        if (amp == std::string_view::npos)
        {
            break;
        }
        pos = amp + 1;
    }
    return out;
}

const char *ReasonPhrase(int code)
{
    switch (code)
    {
    case 200:
        return "OK";
    case 201:
        return "Created";
    case 202:
        return "Accepted";
    case 204:
        return "No Content";
    case 206:
        return "Partial Content";
    case 301:
        return "Moved Permanently";
    case 302:
        return "Found";
    case 303:
        return "See Other";
    case 304:
        return "Not Modified";
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
    case 405:
        return "Method Not Allowed";
    case 408:
        return "Request Timeout";
    case 413:
        return "Payload Too Large";
    case 416:
        return "Range Not Satisfiable";
    case 431:
        return "Request Header Fields Too Large";
    case 500:
        return "Internal Server Error";
    case 501:
        return "Not Implemented";
    case 503:
        return "Service Unavailable";
    default:
        return "OK";
    }
}

std::string MimeType(const std::string &path)
{
    const auto dot = path.find_last_of('.');
    std::string ext;
    if (dot != std::string::npos)
    {
        ext = path.substr(dot + 1);
        for (char &c : ext)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    static const std::map<std::string, std::string> kTypes = {
        {"html", "text/html; charset=utf-8"},
        {"htm", "text/html; charset=utf-8"},
        {"css", "text/css; charset=utf-8"},
        {"js", "text/javascript; charset=utf-8"},
        {"mjs", "text/javascript; charset=utf-8"},
        {"json", "application/json"},
        {"txt", "text/plain; charset=utf-8"},
        {"md", "text/markdown; charset=utf-8"},
        {"xml", "application/xml"},
        {"svg", "image/svg+xml"},
        {"png", "image/png"},
        {"jpg", "image/jpeg"},
        {"jpeg", "image/jpeg"},
        {"gif", "image/gif"},
        {"webp", "image/webp"},
        {"ico", "image/x-icon"},
        {"pdf", "application/pdf"},
        {"zip", "application/zip"},
        {"gz", "application/gzip"},
        {"wasm", "application/wasm"},
        {"woff", "font/woff"},
        {"woff2", "font/woff2"},
        {"ttf", "font/ttf"},
        {"mp4", "video/mp4"},
        {"mp3", "audio/mpeg"},
        {"csv", "text/csv; charset=utf-8"},
    };
    auto it = kTypes.find(ext);
    return it != kTypes.end() ? it->second : "application/octet-stream";
}

// Format a time_t as an HTTP-date (RFC 7231, always GMT).
std::string HttpDate(std::time_t t)
{
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[40];
    const std::size_t n = std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm);
    return std::string(buf, n);
}

std::string HttpDateNow()
{
    return HttpDate(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
}

// Best-effort conversion of a filesystem timestamp to time_t (C++17 lacks a
// portable clock_cast). Accurate to the second, which is all HTTP-date needs.
std::time_t FileTimeToTimeT(fs::file_time_type ftime)
{
    const auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::system_clock::to_time_t(sys);
}

std::string HtmlEscape(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        switch (c)
        {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '\'':
            out += "&#39;";
            break;
        default:
            out.push_back(c);
        }
    }
    return out;
}

// --- Connection stream (plain TCP now; TLS slot in for a later slice) -------

class ServerStream
{
  public:
    virtual ~ServerStream() = default;
    // Reads up to len bytes; Ok(0) means the peer closed (EOF).
    virtual Result<std::size_t> Read(void *buf, std::size_t len,
                                     std::chrono::milliseconds timeout) = 0;
    virtual Result<void> Write(const void *buf, std::size_t len,
                               std::chrono::milliseconds timeout) = 0;
};

class TcpStream final : public ServerStream
{
  public:
    explicit TcpStream(detail::TcpSocket sock) : sock_(std::move(sock))
    {
    }

    Result<std::size_t> Read(void *buf, std::size_t len, std::chrono::milliseconds timeout) override
    {
        return sock_.RecvSome(buf, len, timeout);
    }

    Result<void> Write(const void *buf, std::size_t len, std::chrono::milliseconds timeout) override
    {
        auto r = sock_.SendAll(buf, len, timeout);
        if (!r)
        {
            return Result<void>::Err(r.error());
        }
        return Result<void>::Ok();
    }

  private:
    detail::TcpSocket sock_;
};

// Server-side TLS stream. Owns the accepted socket (stable address) and a TLS
// session whose BIO points at that socket.
class TlsStream final : public ServerStream
{
  public:
    explicit TlsStream(detail::TcpSocket sock) : sock_(std::move(sock))
    {
    }

    Result<void> Handshake(const detail::TlsServerContext &ctx, std::chrono::milliseconds timeout)
    {
        return session_.Handshake(sock_, ctx, timeout);
    }

    Result<std::size_t> Read(void *buf, std::size_t len, std::chrono::milliseconds timeout) override
    {
        return session_.Read(buf, len, timeout);
    }

    Result<void> Write(const void *buf, std::size_t len, std::chrono::milliseconds timeout) override
    {
        auto r = session_.Write(buf, len, timeout);
        if (!r)
        {
            return Result<void>::Err(r.error());
        }
        return Result<void>::Ok();
    }

  private:
    detail::TcpSocket sock_;
    detail::TlsServerSession session_;
};

// --- Buffered reader over a ServerStream -----------------------------------

// Outcome of trying to read one request from a (possibly keep-alive) connection.
enum class ReadOutcome
{
    Request,    // a full request was parsed
    Closed,     // peer closed / idle timeout before a request (normal end)
    BadRequest, // malformed request line or headers
    HeaderTooLarge,
    BodyTooLarge,
};

struct RequestRead
{
    ReadOutcome outcome = ReadOutcome::Closed;
    ServerRequest request;
    bool http_1_0 = false; // client spoke HTTP/1.0 (keep-alive off by default)
};

class ConnReader
{
  public:
    ConnReader(ServerStream &stream, const ServerOptions &opt) : stream_(stream), opt_(opt)
    {
    }

    RequestRead ReadRequest(bool first_request)
    {
        RequestRead result;

        // Wait for the request head. The first byte may wait a long keep-alive
        // idle period; once bytes arrive, the whole head must land within
        // read_timeout.
        const auto first_wait = first_request ? opt_.read_timeout : opt_.keep_alive_timeout;

        std::size_t head_end = FindHeadEnd();
        Clock::time_point deadline{};
        bool have_deadline = false;

        while (head_end == std::string::npos)
        {
            const bool nothing_yet = buf_.empty();
            std::chrono::milliseconds wait = first_wait;
            if (!nothing_yet)
            {
                if (!have_deadline)
                {
                    deadline = Clock::now() + opt_.read_timeout;
                    have_deadline = true;
                }
                wait =
                    std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
                if (wait.count() <= 0)
                {
                    result.outcome = ReadOutcome::Closed;
                    return result;
                }
            }

            char chunk[4096];
            auto r = stream_.Read(chunk, sizeof(chunk), wait);
            if (!r)
            {
                // Timeout or I/O error: treat as a closed connection.
                result.outcome = ReadOutcome::Closed;
                return result;
            }
            const std::size_t n = *r;
            if (n == 0)
            {
                result.outcome = ReadOutcome::Closed;
                return result;
            }
            buf_.append(chunk, n);
            if (buf_.size() > opt_.max_header_bytes)
            {
                result.outcome = ReadOutcome::HeaderTooLarge;
                return result;
            }
            head_end = FindHeadEnd();
        }

        // Parse the head (request line + headers), then the body.
        if (!ParseHead(buf_.substr(0, head_end), result.request, result.http_1_0))
        {
            result.outcome = ReadOutcome::BadRequest;
            return result;
        }
        buf_.erase(0, head_end + 4); // drop head plus the trailing CRLFCRLF

        if (!ReadBody(result))
        {
            return result; // outcome already set
        }

        result.outcome = ReadOutcome::Request;
        return result;
    }

  private:
    std::size_t FindHeadEnd() const
    {
        return buf_.find("\r\n\r\n");
    }

    static bool ParseHead(std::string_view head, ServerRequest &req, bool &http_1_0)
    {
        std::size_t line_end = head.find("\r\n");
        std::string_view request_line = head.substr(0, line_end);
        // METHOD SP target SP HTTP/x.y
        std::size_t sp1 = request_line.find(' ');
        if (sp1 == std::string_view::npos)
        {
            return false;
        }
        std::size_t sp2 = request_line.find(' ', sp1 + 1);
        if (sp2 == std::string_view::npos)
        {
            return false;
        }
        std::string_view method = request_line.substr(0, sp1);
        std::string_view target = request_line.substr(sp1 + 1, sp2 - sp1 - 1);
        std::string_view version = request_line.substr(sp2 + 1);
        if (method.empty() || target.empty() || version.substr(0, 5) != "HTTP/")
        {
            return false;
        }
        http_1_0 = (version == "HTTP/1.0");

        req.method_text = std::string{method};
        if (auto m = ParseMethod(method))
        {
            req.method = *m;
        }
        req.target = std::string{target};

        std::size_t q = target.find('?');
        if (q == std::string_view::npos)
        {
            req.path = PercentDecode(target, false);
        }
        else
        {
            req.path = PercentDecode(target.substr(0, q), false);
            req.params = ParseQuery(target.substr(q + 1));
        }

        // Headers.
        if (line_end == std::string_view::npos)
        {
            return true;
        }
        std::size_t pos = line_end + 2;
        while (pos < head.size())
        {
            std::size_t eol = head.find("\r\n", pos);
            std::string_view line = head.substr(
                pos, eol == std::string_view::npos ? std::string_view::npos : eol - pos);
            if (line.empty())
            {
                break;
            }
            std::size_t colon = line.find(':');
            if (colon != std::string_view::npos)
            {
                std::string_view name = TrimWs(line.substr(0, colon));
                std::string_view value = TrimWs(line.substr(colon + 1));
                if (!name.empty())
                {
                    req.headers.push_back(Header{std::string{name}, std::string{value}});
                }
            }
            if (eol == std::string_view::npos)
            {
                break;
            }
            pos = eol + 2;
        }
        return true;
    }

    // Read the body according to Content-Length / Transfer-Encoding.
    bool ReadBody(RequestRead &result)
    {
        ServerRequest &req = result.request;
        const std::string te = req.header("Transfer-Encoding");
        const std::string cl = req.header("Content-Length");

        if (EqualsIgnoreCase(TrimWs(te), "chunked"))
        {
            return ReadChunkedBody(result);
        }
        if (!cl.empty())
        {
            std::size_t want = 0;
            try
            {
                want = static_cast<std::size_t>(std::stoull(cl));
            }
            catch (...)
            {
                result.outcome = ReadOutcome::BadRequest;
                return false;
            }
            if (want > opt_.max_body_bytes && opt_.max_body_bytes != 0)
            {
                result.outcome = ReadOutcome::BodyTooLarge;
                return false;
            }
            if (!Ensure(want))
            {
                result.outcome = ReadOutcome::Closed;
                return false;
            }
            req.body.assign(buf_.data(), want);
            buf_.erase(0, want);
        }
        return true;
    }

    bool ReadChunkedBody(RequestRead &result)
    {
        ServerRequest &req = result.request;
        std::string body;
        for (;;)
        {
            // Read a chunk-size line.
            std::size_t eol;
            while ((eol = buf_.find("\r\n")) == std::string::npos)
            {
                if (!FillMore())
                {
                    result.outcome = ReadOutcome::Closed;
                    return false;
                }
                if (buf_.size() > opt_.max_header_bytes)
                {
                    result.outcome = ReadOutcome::BadRequest;
                    return false;
                }
            }
            std::string size_line = buf_.substr(0, eol);
            buf_.erase(0, eol + 2);
            std::size_t semi = size_line.find(';');
            if (semi != std::string::npos)
            {
                size_line.erase(semi);
            }
            std::size_t chunk_size = 0;
            for (char c : TrimWs(size_line))
            {
                const int hv = HexValue(c);
                if (hv < 0)
                {
                    result.outcome = ReadOutcome::BadRequest;
                    return false;
                }
                chunk_size = chunk_size * 16 + static_cast<std::size_t>(hv);
            }
            if (chunk_size == 0)
            {
                // Consume the trailing CRLF (and any trailers up to a blank line).
                std::size_t end;
                while ((end = buf_.find("\r\n")) == std::string::npos)
                {
                    if (!FillMore())
                    {
                        break;
                    }
                }
                if (end != std::string::npos)
                {
                    buf_.erase(0, end + 2);
                }
                break;
            }
            if (opt_.max_body_bytes != 0 && body.size() + chunk_size > opt_.max_body_bytes)
            {
                result.outcome = ReadOutcome::BodyTooLarge;
                return false;
            }
            if (!Ensure(chunk_size + 2))
            {
                result.outcome = ReadOutcome::Closed;
                return false;
            }
            body.append(buf_.data(), chunk_size);
            buf_.erase(0, chunk_size + 2); // chunk data + CRLF
        }
        req.body = std::move(body);
        return true;
    }

    // Ensure at least n bytes are buffered.
    bool Ensure(std::size_t n)
    {
        while (buf_.size() < n)
        {
            if (!FillMore())
            {
                return false;
            }
        }
        return true;
    }

    bool FillMore()
    {
        char chunk[8192];
        auto r = stream_.Read(chunk, sizeof(chunk), opt_.read_timeout);
        if (!r || *r == 0)
        {
            return false;
        }
        buf_.append(chunk, *r);
        return true;
    }

    ServerStream &stream_;
    const ServerOptions &opt_;
    std::string buf_;
};

// --- Response writing ------------------------------------------------------

bool HasHeader(const std::vector<Header> &headers, std::string_view name)
{
    for (const auto &h : headers)
    {
        if (EqualsIgnoreCase(h.name, name))
        {
            return true;
        }
    }
    return false;
}

// Serialize and send a response. Returns false on write error (close the conn).
bool WriteResponse(ServerStream &stream, ServerResponse &resp, const ServerOptions &opt,
                   bool keep_alive, bool head_request)
{
    const int code = resp.status_code;
    const std::string reason = resp.reason.empty() ? ReasonPhrase(code) : resp.reason;

    const bool streaming = static_cast<bool>(resp.body_producer);
    const bool has_length = HasHeader(resp.headers, "Content-Length");
    const bool bodyless = code == 204 || code == 304;

    // Decide framing.
    bool chunked = false;
    if (!bodyless)
    {
        if (streaming && !has_length)
        {
            chunked = true;
        }
    }

    std::string head;
    head.reserve(256);
    head += "HTTP/1.1 ";
    head += std::to_string(code);
    head += ' ';
    head += reason;
    head += "\r\n";

    for (const auto &h : resp.headers)
    {
        // These are managed by the server; skip caller duplicates.
        if (EqualsIgnoreCase(h.name, "Connection") ||
            EqualsIgnoreCase(h.name, "Transfer-Encoding") || EqualsIgnoreCase(h.name, "Date") ||
            EqualsIgnoreCase(h.name, "Server"))
        {
            continue;
        }
        head += h.name;
        head += ": ";
        head += h.value;
        head += "\r\n";
    }

    if (!bodyless && !streaming && !has_length)
    {
        head += "Content-Length: ";
        head += std::to_string(resp.body.size());
        head += "\r\n";
    }
    if (chunked)
    {
        head += "Transfer-Encoding: chunked\r\n";
    }

    head += "Date: ";
    head += HttpDateNow();
    head += "\r\n";
    head += "Server: ";
    head += opt.server_name;
    head += "\r\n";
    head += "Connection: ";
    head += keep_alive ? "keep-alive" : "close";
    head += "\r\n\r\n";

    if (!stream.Write(head.data(), head.size(), opt.write_timeout))
    {
        return false;
    }

    if (head_request || bodyless)
    {
        return true;
    }

    if (streaming)
    {
        bool ok = true;
        if (chunked)
        {
            ResponseSink sink = [&](std::string_view data) -> Result<void> {
                if (data.empty())
                {
                    return Result<void>::Ok();
                }
                char size_line[24];
                const int m = std::snprintf(size_line, sizeof(size_line), "%zx\r\n", data.size());
                if (m <= 0 ||
                    !stream.Write(size_line, static_cast<std::size_t>(m), opt.write_timeout))
                {
                    return Result<void>::Err(Error{ErrorCode::IoError, "write failed"});
                }
                if (!stream.Write(data.data(), data.size(), opt.write_timeout) ||
                    !stream.Write("\r\n", 2, opt.write_timeout))
                {
                    return Result<void>::Err(Error{ErrorCode::IoError, "write failed"});
                }
                return Result<void>::Ok();
            };
            auto pr = resp.body_producer(sink);
            ok = static_cast<bool>(pr);
            if (ok)
            {
                ok = static_cast<bool>(stream.Write("0\r\n\r\n", 5, opt.write_timeout));
            }
        }
        else
        {
            ResponseSink sink = [&](std::string_view data) -> Result<void> {
                if (!data.empty() && !stream.Write(data.data(), data.size(), opt.write_timeout))
                {
                    return Result<void>::Err(Error{ErrorCode::IoError, "write failed"});
                }
                return Result<void>::Ok();
            };
            ok = static_cast<bool>(resp.body_producer(sink));
        }
        return ok;
    }

    if (!resp.body.empty())
    {
        return static_cast<bool>(
            stream.Write(resp.body.data(), resp.body.size(), opt.write_timeout));
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// ServerRequest / ServerResponse
// ---------------------------------------------------------------------------

std::string ServerRequest::header(std::string_view name) const
{
    for (const auto &h : headers)
    {
        if (EqualsIgnoreCase(h.name, name))
        {
            return h.value;
        }
    }
    return {};
}

ServerResponse &ServerResponse::set_header(std::string name, std::string value)
{
    for (auto &h : headers)
    {
        if (EqualsIgnoreCase(h.name, name))
        {
            h.value = std::move(value);
            return *this;
        }
    }
    headers.push_back(Header{std::move(name), std::move(value)});
    return *this;
}

std::string ServerResponse::header(std::string_view name) const
{
    for (const auto &h : headers)
    {
        if (EqualsIgnoreCase(h.name, name))
        {
            return h.value;
        }
    }
    return {};
}

ServerResponse ServerResponse::Text(int status, std::string body, std::string content_type)
{
    ServerResponse r;
    r.status_code = status;
    r.body = std::move(body);
    r.set_header("Content-Type", std::move(content_type));
    return r;
}

ServerResponse ServerResponse::Json(int status, std::string json)
{
    ServerResponse r;
    r.status_code = status;
    r.body = std::move(json);
    r.set_header("Content-Type", "application/json");
    return r;
}

ServerResponse ServerResponse::Status(int status)
{
    ServerResponse r;
    r.status_code = status;
    return r;
}

ServerResponse ServerResponse::NotFound(std::string body)
{
    return Text(404, std::move(body));
}

ServerResponse ServerResponse::Redirect(int status, std::string location)
{
    ServerResponse r;
    r.status_code = status;
    r.set_header("Location", std::move(location));
    return r;
}

Result<ServerResponse> ServerResponse::File(const std::string &path)
{
    std::error_code ec;
    if (!fs::is_regular_file(path, ec))
    {
        return Result<ServerResponse>::Err(Error{ErrorCode::FileError, "not a file: " + path});
    }
    const auto size = fs::file_size(path, ec);
    if (ec)
    {
        return Result<ServerResponse>::Err(Error{ErrorCode::FileError, "cannot stat: " + path});
    }

    ServerResponse r;
    r.status_code = 200;
    r.set_header("Content-Type", MimeType(path));
    r.set_header("Content-Length", std::to_string(size));
    const std::string file_path = path;
    r.body_producer = [file_path](const ResponseSink &sink) -> Result<void> {
        std::ifstream in(file_path, std::ios::binary);
        if (!in)
        {
            return Result<void>::Err(Error{ErrorCode::FileError, "cannot open: " + file_path});
        }
        std::array<char, 64 * 1024> chunk;
        while (in)
        {
            in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
            const auto got = in.gcount();
            if (got > 0)
            {
                auto w = sink(std::string_view(chunk.data(), static_cast<std::size_t>(got)));
                if (!w)
                {
                    return w;
                }
            }
        }
        return Result<void>::Ok();
    };
    return Result<ServerResponse>::Ok(std::move(r));
}

// ---------------------------------------------------------------------------
// TlsServerConfig
// ---------------------------------------------------------------------------

Result<TlsServerConfig> TlsServerConfig::FromFiles(const std::string &cert_path,
                                                   const std::string &key_path,
                                                   const std::string &key_password)
{
    auto ctx = detail::TlsServerContext::FromFiles(cert_path, key_path, key_password);
    if (!ctx)
    {
        return Result<TlsServerConfig>::Err(ctx.error());
    }
    TlsServerConfig cfg;
    cfg.enabled = true;
    cfg.cert_pem = ctx->cert_pem();
    cfg.key_pem = ctx->key_pem();
    cfg.key_password = ctx->key_password();
    return Result<TlsServerConfig>::Ok(std::move(cfg));
}

Result<TlsServerConfig> TlsServerConfig::SelfSigned(const std::string &common_name)
{
    auto ctx = detail::TlsServerContext::SelfSigned(common_name);
    if (!ctx)
    {
        return Result<TlsServerConfig>::Err(ctx.error());
    }
    TlsServerConfig cfg;
    cfg.enabled = true;
    cfg.cert_pem = ctx->cert_pem();
    cfg.key_pem = ctx->key_pem();
    return Result<TlsServerConfig>::Ok(std::move(cfg));
}

// ---------------------------------------------------------------------------
// Server::Impl
// ---------------------------------------------------------------------------

struct StaticMount
{
    std::string prefix;
    std::string directory;
    StaticOptions options;
};

struct Server::Impl
{
    explicit Impl(ServerOptions o) : options(std::move(o))
    {
    }

    ~Impl()
    {
        Stop();
    }

    ServerOptions options;
    std::map<std::string, Handler> routes; // key: "METHOD path"
    std::vector<StaticMount> mounts;       // sorted by prefix length, longest first
    Handler default_handler;

    detail::TcpListener listener;
    std::unique_ptr<detail::TlsServerContext> tls_context; // null for plain HTTP
    std::vector<std::thread> workers;
    std::thread accept_thread;

    std::deque<std::pair<detail::TcpSocket, std::string>> queue;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;

    std::atomic<bool> stopping{false};
    std::atomic<bool> running{false};
    std::mutex life_mutex;
    std::condition_variable life_cv;

    static std::string RouteKey(Method method, const std::string &path)
    {
        return std::string{ToString(method)} + " " + path;
    }

    Result<void> Start()
    {
        if (running.load())
        {
            return Result<void>::Err(Error{ErrorCode::InvalidArgument, "server already running"});
        }

        if (options.tls.enabled)
        {
            auto ctx = detail::TlsServerContext::FromPem(options.tls.cert_pem, options.tls.key_pem,
                                                         options.tls.key_password);
            if (!ctx)
            {
                return Result<void>::Err(ctx.error());
            }
            tls_context = std::make_unique<detail::TlsServerContext>(std::move(*ctx));
        }

        auto bound = detail::TcpListener::Bind(options.bind_address, options.port, options.backlog);
        if (!bound)
        {
            return Result<void>::Err(bound.error());
        }
        listener = std::move(*bound);

        // Longest prefix first so "/static" wins over "/".
        std::sort(mounts.begin(), mounts.end(), [](const StaticMount &a, const StaticMount &b) {
            return a.prefix.size() > b.prefix.size();
        });

        stopping.store(false);
        running.store(true);

        unsigned n = options.threads;
        if (n == 0)
        {
            n = std::thread::hardware_concurrency();
            if (n == 0)
            {
                n = 4;
            }
        }
        workers.reserve(n);
        for (unsigned i = 0; i < n; ++i)
        {
            workers.emplace_back([this] { WorkerLoop(); });
        }
        accept_thread = std::thread([this] { AcceptLoop(); });
        return Result<void>::Ok();
    }

    void Stop()
    {
        if (!running.exchange(false))
        {
            // Not running; ensure any spawned threads are joined defensively.
            if (accept_thread.joinable())
            {
                accept_thread.join();
            }
            return;
        }
        stopping.store(true);
        listener.Close();
        queue_cv.notify_all();

        if (accept_thread.joinable())
        {
            accept_thread.join();
        }
        for (auto &w : workers)
        {
            if (w.joinable())
            {
                w.join();
            }
        }
        workers.clear();
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            queue.clear();
        }
        life_cv.notify_all();
    }

    void Wait()
    {
        std::unique_lock<std::mutex> lock(life_mutex);
        life_cv.wait(lock, [this] { return !running.load(); });
    }

    void AcceptLoop()
    {
        while (!stopping.load())
        {
            std::string peer;
            auto s = listener.Accept(std::chrono::milliseconds(200), &peer);
            if (s)
            {
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    queue.emplace_back(std::move(*s), std::move(peer));
                }
                queue_cv.notify_one();
            }
            // Timeout / transient errors: loop and re-check the stop flag.
        }
    }

    void WorkerLoop()
    {
        for (;;)
        {
            std::pair<detail::TcpSocket, std::string> conn;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                queue_cv.wait(lock, [this] { return stopping.load() || !queue.empty(); });
                if (queue.empty())
                {
                    if (stopping.load())
                    {
                        return;
                    }
                    continue;
                }
                conn = std::move(queue.front());
                queue.pop_front();
            }
            HandleConnection(std::move(conn.first), conn.second);
        }
    }

    void HandleConnection(detail::TcpSocket sock, const std::string &peer)
    {
        std::unique_ptr<ServerStream> stream;
        if (tls_context)
        {
            auto tls = std::make_unique<TlsStream>(std::move(sock));
            auto hs = tls->Handshake(*tls_context, options.read_timeout);
            if (!hs)
            {
                return; // handshake failed; drop the connection
            }
            stream = std::move(tls);
        }
        else
        {
            stream = std::make_unique<TcpStream>(std::move(sock));
        }
        ConnReader reader(*stream, options);

        int served = 0;
        while (served < options.max_keep_alive_requests && !stopping.load())
        {
            RequestRead rr = reader.ReadRequest(served == 0);
            if (rr.outcome == ReadOutcome::Closed)
            {
                return;
            }
            if (rr.outcome != ReadOutcome::Request)
            {
                int code = 400;
                if (rr.outcome == ReadOutcome::HeaderTooLarge)
                {
                    code = 431;
                }
                else if (rr.outcome == ReadOutcome::BodyTooLarge)
                {
                    code = 413;
                }
                ServerResponse err = ServerResponse::Text(code, ReasonPhrase(code));
                WriteResponse(*stream, err, options, false, false);
                return;
            }

            ServerRequest &req = rr.request;
            req.client_address = peer;

            // Keep-alive decision: HTTP/1.1 keeps alive unless "Connection: close";
            // HTTP/1.0 closes unless the client explicitly asks to keep alive.
            const std::string conn_hdr = req.header("Connection");
            const std::string_view conn_tok = TrimWs(conn_hdr);
            bool keep_alive = rr.http_1_0 ? EqualsIgnoreCase(conn_tok, "keep-alive")
                                          : !EqualsIgnoreCase(conn_tok, "close");
            if (served + 1 >= options.max_keep_alive_requests)
            {
                keep_alive = false;
            }

            const bool head_request = req.method == Method::Head;
            ServerResponse resp = Dispatch(req);

            if (!WriteResponse(*stream, resp, options, keep_alive, head_request))
            {
                return;
            }
            ++served;
            if (!keep_alive)
            {
                return;
            }
        }
    }

    ServerResponse Dispatch(const ServerRequest &req)
    {
        // 1. Exact method+path route. HEAD falls back to a GET route.
        Method lookup = req.method;
        auto it = routes.find(RouteKey(lookup, req.path));
        if (it == routes.end() && req.method == Method::Head)
        {
            it = routes.find(RouteKey(Method::Get, req.path));
        }
        if (it != routes.end())
        {
            return it->second(req);
        }

        // 2. Static mounts (longest matching prefix).
        for (const auto &mount : mounts)
        {
            if (PathHasPrefix(req.path, mount.prefix))
            {
                return ServeStatic(req, mount);
            }
        }

        // 3. Default handler or 404.
        if (default_handler)
        {
            return default_handler(req);
        }
        return ServerResponse::NotFound();
    }

    static bool PathHasPrefix(const std::string &path, const std::string &prefix)
    {
        if (prefix == "/")
        {
            return true;
        }
        if (path.rfind(prefix, 0) != 0)
        {
            return false;
        }
        return path.size() == prefix.size() || path[prefix.size()] == '/';
    }

    ServerResponse ServeStatic(const ServerRequest &req, const StaticMount &mount)
    {
        // Relative path under the mount.
        std::string rel = req.path.substr(mount.prefix == "/" ? 0 : mount.prefix.size());
        while (!rel.empty() && rel.front() == '/')
        {
            rel.erase(0, 1);
        }

        std::error_code ec;
        const fs::path base = fs::weakly_canonical(fs::path(mount.directory), ec);
        fs::path target = fs::weakly_canonical(base / fs::path(rel), ec);

        // Reject traversal outside the served directory.
        const std::string base_str = base.string();
        const std::string target_str = target.string();
        if (target_str != base_str &&
            target_str.rfind(base_str + static_cast<char>(fs::path::preferred_separator), 0) != 0)
        {
            return ServerResponse::NotFound();
        }

        if (fs::is_directory(target, ec))
        {
            if (!mount.options.index_file.empty())
            {
                fs::path index = target / mount.options.index_file;
                if (fs::is_regular_file(index, ec))
                {
                    return ServeFile(req, index.string());
                }
            }
            if (mount.options.directory_listing)
            {
                return DirectoryListing(req.path, target);
            }
            return ServerResponse::Status(403);
        }

        if (fs::is_regular_file(target, ec))
        {
            return ServeFile(req, target.string());
        }
        return ServerResponse::NotFound();
    }

    ServerResponse ServeFile(const ServerRequest &req, const std::string &path)
    {
        std::error_code ec;
        const auto size = fs::file_size(path, ec);
        if (ec)
        {
            return ServerResponse::NotFound();
        }
        const std::time_t mtime = FileTimeToTimeT(fs::last_write_time(path, ec));
        const std::string last_modified = HttpDate(mtime);

        // Conditional GET.
        const std::string ims = req.header("If-Modified-Since");
        if (!ims.empty() && ims == last_modified)
        {
            ServerResponse nm = ServerResponse::Status(304);
            nm.set_header("Last-Modified", last_modified);
            return nm;
        }

        // Range request (single range only).
        const std::string range = req.header("Range");
        std::uint64_t start = 0;
        std::uint64_t end = size == 0 ? 0 : size - 1;
        bool is_range = false;
        if (!range.empty() && range.rfind("bytes=", 0) == 0 && size > 0)
        {
            const std::string spec = range.substr(6);
            const std::size_t dash = spec.find('-');
            if (dash != std::string::npos)
            {
                const std::string s = spec.substr(0, dash);
                const std::string e = spec.substr(dash + 1);
                try
                {
                    if (s.empty() && !e.empty())
                    {
                        // Suffix range: last N bytes.
                        const std::uint64_t n = std::stoull(e);
                        start = n >= size ? 0 : size - n;
                        end = size - 1;
                        is_range = true;
                    }
                    else if (!s.empty())
                    {
                        start = std::stoull(s);
                        end = e.empty() ? size - 1 : std::stoull(e);
                        is_range = true;
                    }
                }
                catch (...)
                {
                    is_range = false;
                }
            }
            if (is_range && (start > end || start >= size))
            {
                ServerResponse bad = ServerResponse::Status(416);
                bad.set_header("Content-Range", "bytes */" + std::to_string(size));
                return bad;
            }
            if (is_range && end >= size)
            {
                end = size - 1;
            }
        }

        ServerResponse r;
        r.set_header("Content-Type", MimeType(path));
        r.set_header("Last-Modified", last_modified);
        r.set_header("Accept-Ranges", "bytes");

        const std::uint64_t length = is_range ? (end - start + 1) : size;
        r.status_code = is_range ? 206 : 200;
        if (is_range)
        {
            r.set_header("Content-Range", "bytes " + std::to_string(start) + "-" +
                                              std::to_string(end) + "/" + std::to_string(size));
        }
        r.set_header("Content-Length", std::to_string(length));

        const std::string file_path = path;
        r.body_producer = [file_path, start, length](const ResponseSink &sink) -> Result<void> {
            std::ifstream in(file_path, std::ios::binary);
            if (!in)
            {
                return Result<void>::Err(Error{ErrorCode::FileError, "cannot open: " + file_path});
            }
            in.seekg(static_cast<std::streamoff>(start));
            std::uint64_t remaining = length;
            std::array<char, 64 * 1024> chunk;
            while (remaining > 0 && in)
            {
                const std::size_t want =
                    static_cast<std::size_t>(std::min<std::uint64_t>(remaining, chunk.size()));
                in.read(chunk.data(), static_cast<std::streamsize>(want));
                const auto got = in.gcount();
                if (got <= 0)
                {
                    break;
                }
                auto w = sink(std::string_view(chunk.data(), static_cast<std::size_t>(got)));
                if (!w)
                {
                    return w;
                }
                remaining -= static_cast<std::uint64_t>(got);
            }
            return Result<void>::Ok();
        };
        return r;
    }

    static ServerResponse DirectoryListing(const std::string &url_path, const fs::path &dir)
    {
        std::string html;
        html += "<!doctype html><html><head><meta charset=\"utf-8\"><title>Index of ";
        html += HtmlEscape(url_path);
        html += "</title></head><body><h1>Index of ";
        html += HtmlEscape(url_path);
        html += "</h1><ul>";

        std::string base = url_path;
        if (base.empty() || base.back() != '/')
        {
            base += '/';
        }
        if (url_path != "/")
        {
            html += "<li><a href=\"../\">../</a></li>";
        }

        std::error_code ec;
        std::vector<std::pair<std::string, bool>> entries; // name, is_dir
        for (const auto &entry : fs::directory_iterator(dir, ec))
        {
            const std::string name = entry.path().filename().string();
            entries.emplace_back(name, entry.is_directory(ec));
        }
        std::sort(entries.begin(), entries.end());
        for (const auto &[name, is_dir] : entries)
        {
            const std::string suffix = is_dir ? "/" : "";
            html += "<li><a href=\"";
            html += HtmlEscape(base + name + suffix);
            html += "\">";
            html += HtmlEscape(name + suffix);
            html += "</a></li>";
        }
        html += "</ul></body></html>";
        return ServerResponse::Text(200, std::move(html), "text/html; charset=utf-8");
    }
};

// ---------------------------------------------------------------------------
// Server
// ---------------------------------------------------------------------------

Server::Server(ServerOptions options) : impl_(std::make_unique<Impl>(std::move(options)))
{
}

Server::~Server() = default;
Server::Server(Server &&) noexcept = default;
Server &Server::operator=(Server &&) noexcept = default;

Server &Server::route(Method method, std::string path, Handler handler)
{
    impl_->routes[Impl::RouteKey(method, path)] = std::move(handler);
    return *this;
}

Server &Server::serve_files(std::string mount_prefix, std::string directory, StaticOptions opt)
{
    if (mount_prefix.empty())
    {
        mount_prefix = "/";
    }
    impl_->mounts.push_back(
        StaticMount{std::move(mount_prefix), std::move(directory), std::move(opt)});
    return *this;
}

Server &Server::set_default_handler(Handler handler)
{
    impl_->default_handler = std::move(handler);
    return *this;
}

Result<void> Server::start()
{
    return impl_->Start();
}

void Server::stop() noexcept
{
    if (impl_)
    {
        impl_->Stop();
    }
}

void Server::wait()
{
    if (impl_)
    {
        impl_->Wait();
    }
}

bool Server::running() const noexcept
{
    return impl_ && impl_->running.load();
}

std::uint16_t Server::port() const noexcept
{
    return impl_ ? impl_->listener.port() : 0;
}

} // namespace mog
