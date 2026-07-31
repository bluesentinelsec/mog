/**
 * @file winhttp_backend.cpp
 * @brief Windows native backend over WinHTTP (Backend::WinHttp).
 *
 * Reuses PrepareRequest so json/form/multipart/auth/cookie encoding matches the
 * embedded backend. WinHTTP handles TLS, redirects, and transparent gzip.
 *
 * Known deltas vs embedded (documented): a custom CA bundle (Options::ca_bundle)
 * and PEM client certificates (mTLS) are not wired here — WinHTTP uses the
 * Windows certificate store. Use the embedded or curl backend for those.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "http/detail/winhttp_backend.hpp"

#include "http/detail/prepare.hpp"
#include "http/detail/url.hpp"
#include "mog/log.hpp"
#include "mog/options.hpp"
#include "mog/response.hpp"
#include "mog/util.hpp"

#include <windows.h>
// winhttp.h must follow windows.h.
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cwchar>
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include <winhttp.h>

namespace mog::detail
{
namespace
{

// RAII for a WinHTTP HINTERNET handle.
class WinHttpHandle
{
  public:
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET h) : h_(h)
    {
    }
    ~WinHttpHandle()
    {
        if (h_ != nullptr)
        {
            WinHttpCloseHandle(h_);
        }
    }
    WinHttpHandle(const WinHttpHandle &) = delete;
    WinHttpHandle &operator=(const WinHttpHandle &) = delete;

    WinHttpHandle &operator=(HINTERNET h)
    {
        if (h_ != nullptr)
        {
            WinHttpCloseHandle(h_);
        }
        h_ = h;
        return *this;
    }

    [[nodiscard]] HINTERNET get() const noexcept
    {
        return h_;
    }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return h_ != nullptr;
    }

  private:
    HINTERNET h_ = nullptr;
};

std::wstring Utf8ToWide(const std::string &s)
{
    if (s.empty())
    {
        return std::wstring{};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string WideToUtf8(const wchar_t *s, int len)
{
    if (len <= 0)
    {
        return std::string{};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, s, len, nullptr, 0, nullptr, nullptr);
    std::string o(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, len, o.data(), n, nullptr, nullptr);
    return o;
}

bool IsSetCookie(const std::string &name)
{
    static const std::string kSetCookie = "set-cookie";
    if (name.size() != kSetCookie.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < name.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(name[i])) != kSetCookie[i])
        {
            return false;
        }
    }
    return true;
}

bool IsAcceptEncoding(const std::string &name)
{
    static const std::string kAccept = "accept-encoding";
    if (name.size() != kAccept.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < name.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(name[i])) != kAccept[i])
        {
            return false;
        }
    }
    return true;
}

std::map<std::string, std::string> CollectCookies(const std::vector<Header> &headers)
{
    std::map<std::string, std::string> cookies;
    for (const auto &h : headers)
    {
        if (!IsSetCookie(h.name))
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

// Parse the CRLF-joined raw response headers, skipping the status line.
std::vector<Header> ParseRawHeaders(const std::string &raw)
{
    std::vector<Header> headers;
    std::size_t pos = 0;
    bool first = true;
    while (pos < raw.size())
    {
        std::size_t end = raw.find("\r\n", pos);
        if (end == std::string::npos)
        {
            end = raw.size();
        }
        const std::string line = raw.substr(pos, end - pos);
        pos = end + 2;
        if (line.empty())
        {
            continue;
        }
        if (first)
        {
            first = false; // status line (e.g. "HTTP/1.1 200 OK")
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
        headers.push_back(Header{std::move(name), std::move(value)});
    }
    return headers;
}

Error MapWinHttpError(DWORD code)
{
    switch (code)
    {
    case ERROR_WINHTTP_TIMEOUT:
        return Error{ErrorCode::Timeout, "WinHTTP: request timed out"};
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        return Error{ErrorCode::DnsFailed, "WinHTTP: host name could not be resolved"};
    case ERROR_WINHTTP_CANNOT_CONNECT:
        return Error{ErrorCode::ConnectFailed, "WinHTTP: cannot connect to server"};
    case ERROR_WINHTTP_CONNECTION_ERROR:
        return Error{ErrorCode::ConnectFailed, "WinHTTP: connection error"};
    case ERROR_WINHTTP_SECURE_FAILURE:
        return Error{ErrorCode::TlsFailed, "WinHTTP: TLS/secure channel failure"};
    case ERROR_WINHTTP_INVALID_URL:
    case ERROR_WINHTTP_UNRECOGNIZED_SCHEME:
        return Error{ErrorCode::InvalidUrl, "WinHTTP: invalid URL"};
    default:
        return Error{ErrorCode::IoError, "WinHTTP error " + std::to_string(code)};
    }
}

class WinHttpTransport final : public Transport
{
  public:
    [[nodiscard]] std::string_view Name() const noexcept override
    {
        return "winhttp";
    }

    [[nodiscard]] bool Available() const noexcept override
    {
        return true; // WinHTTP ships with Windows
    }

    [[nodiscard]] bool AutoPreferred() const noexcept override
    {
        return true;
    }

    // WinHTTP here does not implement streaming, Digest, or PEM CA/mTLS (it uses
    // the Windows cert store); Auto falls back to embedded for those.
    [[nodiscard]] bool Supports(const Options &options) const noexcept override
    {
        // Intentional delta: WinHTTP uses the Windows cert store, so a PEM CA
        // bundle or PEM client certificate routes to a PEM-capable backend.
        if (options.ca_bundle.has_value() || options.client_cert.has_value())
        {
            return false;
        }
        return true;
    }

    [[nodiscard]] Result<Response> Execute(Method method, std::string_view url,
                                           const Options &options) override
    {
        const PreparedRequest prepared = PrepareRequest(options);
        const std::string full_url = AppendQuery(url, options.params);
        const auto started = std::chrono::steady_clock::now();

        const std::wstring wurl = Utf8ToWide(full_url);
        URL_COMPONENTS uc{};
        uc.dwStructSize = sizeof(uc);
        uc.dwSchemeLength = static_cast<DWORD>(-1);
        uc.dwHostNameLength = static_cast<DWORD>(-1);
        uc.dwUrlPathLength = static_cast<DWORD>(-1);
        uc.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc))
        {
            return Result<Response>::Err(Error{ErrorCode::InvalidUrl, "invalid url: " + full_url});
        }
        const std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
        std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
        if (uc.dwExtraInfoLength > 0)
        {
            path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
        }
        if (path.empty())
        {
            path = L"/";
        }
        const bool https = uc.nScheme == INTERNET_SCHEME_HTTPS;

        WinHttpHandle session{WinHttpOpen(L"mog", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
        if (!session)
        {
            return Result<Response>::Err(MapWinHttpError(GetLastError()));
        }

        const auto timeout_ms = static_cast<int>(IoTimeout(options).count());
        const auto connect_ms = static_cast<int>(ConnectTimeout(options).count());
        WinHttpSetTimeouts(session.get(), connect_ms, connect_ms, timeout_ms, timeout_ms);

        WinHttpHandle connect{
            WinHttpConnect(session.get(), host.c_str(), static_cast<INTERNET_PORT>(uc.nPort), 0)};
        if (!connect)
        {
            return Result<Response>::Err(MapWinHttpError(GetLastError()));
        }

        const DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
        const std::wstring wmethod = Utf8ToWide(std::string{ToString(method)});
        WinHttpHandle request{WinHttpOpenRequest(connect.get(), wmethod.c_str(), path.c_str(),
                                                 nullptr, WINHTTP_NO_REFERER,
                                                 WINHTTP_DEFAULT_ACCEPT_TYPES, flags)};
        if (!request)
        {
            return Result<Response>::Err(MapWinHttpError(GetLastError()));
        }

        if (!options.allow_redirects)
        {
            DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
            WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &policy,
                             sizeof(policy));
        }
        if (!options.verify_tls)
        {
            DWORD sec = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                        SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                        SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
            WinHttpSetOption(request.get(), WINHTTP_OPTION_SECURITY_FLAGS, &sec, sizeof(sec));
        }
#ifdef WINHTTP_OPTION_DECOMPRESSION
        if (options.decompress)
        {
            DWORD decomp = WINHTTP_DECOMPRESSION_FLAG_ALL;
            // Available on Windows 8.1+ SDKs; ignore failure on older systems.
            WinHttpSetOption(request.get(), WINHTTP_OPTION_DECOMPRESSION, &decomp, sizeof(decomp));
        }
#endif

        std::wstring headers_w;
        for (const auto &h : prepared.headers)
        {
            if (options.decompress && IsAcceptEncoding(h.first))
            {
                continue; // let WinHTTP negotiate + decode
            }
            headers_w += Utf8ToWide(h.first + ": " + h.second + "\r\n");
        }

        LPCWSTR header_ptr = headers_w.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers_w.c_str();
        const DWORD header_len = headers_w.empty() ? 0 : static_cast<DWORD>(-1);
        LPVOID body_ptr = prepared.body.empty()
                              ? WINHTTP_NO_REQUEST_DATA
                              : const_cast<LPVOID>(static_cast<LPCVOID>(prepared.body.data()));
        const DWORD body_len = static_cast<DWORD>(prepared.body.size());

        const std::wstring wuser = Utf8ToWide(options.auth.username);
        const std::wstring wpass = Utf8ToWide(options.auth.password);
        const bool digest = options.auth.kind == Auth::Kind::Digest;

        DWORD status = 0;
        // Up to two passes: the second answers a Digest 401 challenge with
        // credentials (WinHTTP computes the response). Non-Digest goes once.
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            if (!WinHttpSendRequest(request.get(), header_ptr, header_len, body_ptr, body_len,
                                    body_len, 0))
            {
                return Result<Response>::Err(MapWinHttpError(GetLastError()));
            }
            if (!WinHttpReceiveResponse(request.get(), nullptr))
            {
                return Result<Response>::Err(MapWinHttpError(GetLastError()));
            }

            status = 0;
            DWORD status_size = sizeof(status);
            WinHttpQueryHeaders(
                request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX);

            if (attempt == 0 && digest && status == 401)
            {
                DWORD supported = 0;
                DWORD first = 0;
                DWORD target = 0;
                if (WinHttpQueryAuthSchemes(request.get(), &supported, &first, &target) &&
                    (supported & WINHTTP_AUTH_SCHEME_DIGEST) != 0)
                {
                    WinHttpSetCredentials(request.get(), WINHTTP_AUTH_TARGET_SERVER,
                                          WINHTTP_AUTH_SCHEME_DIGEST, wuser.c_str(), wpass.c_str(),
                                          nullptr);
                    continue; // resend with credentials
                }
            }
            break;
        }

        std::vector<Header> response_headers;
        DWORD headers_size = 0;
        WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_RAW_HEADERS_CRLF,
                            WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &headers_size,
                            WINHTTP_NO_HEADER_INDEX);
        if (headers_size > 0)
        {
            std::wstring raw(headers_size / sizeof(wchar_t), L'\0');
            if (WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_RAW_HEADERS_CRLF,
                                    WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &headers_size,
                                    WINHTTP_NO_HEADER_INDEX))
            {
                const std::string raw_utf8 =
                    WideToUtf8(raw.c_str(), static_cast<int>(wcslen(raw.c_str())));
                response_headers = ParseRawHeaders(raw_utf8);
            }
        }

        const bool streaming = static_cast<bool>(options.response_writer);
        std::string body;
        std::size_t received = 0;
        for (;;)
        {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(request.get(), &avail))
            {
                return Result<Response>::Err(MapWinHttpError(GetLastError()));
            }
            if (avail == 0)
            {
                break;
            }
            std::string chunk(avail, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request.get(), chunk.data(), avail, &read))
            {
                return Result<Response>::Err(MapWinHttpError(GetLastError()));
            }
            if (read == 0)
            {
                break;
            }
            if (options.max_response_bytes > 0 &&
                received + static_cast<std::size_t>(read) > options.max_response_bytes)
            {
                return Result<Response>::Err(
                    Error{ErrorCode::ResponseTooLarge, "response exceeds max_response_bytes"});
            }
            received += static_cast<std::size_t>(read);
            if (streaming)
            {
                auto w = options.response_writer(std::string_view(chunk.data(), read));
                if (!w)
                {
                    return Result<Response>::Err(w.error());
                }
            }
            else
            {
                body.append(chunk.data(), read);
            }
        }

        std::string effective_url = full_url;
        DWORD url_size = 0;
        WinHttpQueryOption(request.get(), WINHTTP_OPTION_URL, nullptr, &url_size);
        if (url_size > 0)
        {
            std::wstring eff(url_size / sizeof(wchar_t), L'\0');
            if (WinHttpQueryOption(request.get(), WINHTTP_OPTION_URL, eff.data(), &url_size))
            {
                effective_url = WideToUtf8(eff.c_str(), static_cast<int>(wcslen(eff.c_str())));
            }
        }

        Response response;
        response.status_code = static_cast<int>(status);
        response.url = effective_url;
        response.headers = std::move(response_headers);
        response.body = std::move(body); // empty when streaming (delivered to the writer)
        response.downloaded_bytes = received;
        response.backend = "winhttp";
        response.cookies = CollectCookies(response.headers);
        response.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
        return Result<Response>::Ok(std::move(response));
    }
};

} // namespace

std::unique_ptr<Transport> MakeWinHttpTransport()
{
    return std::make_unique<WinHttpTransport>();
}

} // namespace mog::detail
