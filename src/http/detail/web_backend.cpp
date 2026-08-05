/**
 * @file web_backend.cpp
 * @brief Emscripten transport backed by the browser Fetch API.
 */

#include "http/detail/web_backend.hpp"

#include "http/detail/prepare.hpp"
#include "http/detail/url.hpp"
#include "mog/log.hpp"

#include <cctype>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdlib>
#include <emscripten.h>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mog::detail
{
namespace
{

// Fetch is asynchronous in browsers. EM_ASYNC_JS and the transitive
// -sASYNCIFY=1 link option preserve mog's synchronous C++ request contract while
// yielding control to the browser event loop during the transfer.
EM_ASYNC_JS(int, MogBrowserFetch,
            (const char *url_ptr, const char *method_ptr, const char *headers_ptr,
             const void *request_body_ptr, std::size_t request_body_size, int timeout_ms,
             int follow_redirects, std::size_t max_response_bytes, int *status_out,
             char **reason_out, char **url_out, char **headers_out, char **body_out,
             std::size_t *body_size_out, char **error_out, int *error_kind_out),
            {
                const writeString = (out, value) => {
                    const size = lengthBytesUTF8(value) + 1;
                    const ptr = _malloc(size);
                    if (!ptr)
                    {
                        throw new Error("WebAssembly allocation failed");
                    }
                    stringToUTF8(value, ptr, size);
                    HEAPU32[out >> 2] = ptr;
                };

                HEAP32[status_out >> 2] = 0;
                HEAPU32[reason_out >> 2] = 0;
                HEAPU32[url_out >> 2] = 0;
                HEAPU32[headers_out >> 2] = 0;
                HEAPU32[body_out >> 2] = 0;
                HEAPU32[body_size_out >> 2] = 0;
                HEAPU32[error_out >> 2] = 0;
                HEAP32[error_kind_out >> 2] = 0;

                const controller = new AbortController();
                let timer = 0;
                try
                {
                    const headers = new Headers();
                    const serializedHeaders = UTF8ToString(headers_ptr);
                    for (const line of serializedHeaders.split("\n"))
                    {
                        if (!line)
                        {
                            continue;
                        }
                        const separator = line.indexOf(":");
                        if (separator > 0)
                        {
                            headers.append(line.slice(0, separator), line.slice(separator + 1));
                        }
                    }

                    const init = {
                        method: UTF8ToString(method_ptr),
                        headers,
                        redirect: follow_redirects ? "follow" : "manual",
                        credentials: "same-origin",
                        signal: controller.signal,
                    };
                    if (request_body_size > 0)
                    {
                        init.body = HEAPU8.slice(request_body_ptr,
                                                request_body_ptr + request_body_size);
                    }
                    if (timeout_ms >= 0)
                    {
                        timer = setTimeout(() => controller.abort(), timeout_ms);
                    }

                    const response = await fetch(UTF8ToString(url_ptr), init);
                    if (response.type === "opaqueredirect")
                    {
                        HEAP32[error_kind_out >> 2] = 3;
                        writeString(error_out,
                                    "browser Fetch does not expose manual redirect responses");
                        return 0;
                    }
                    const contentLength = Number(response.headers.get("content-length"));
                    if (max_response_bytes > 0 && Number.isFinite(contentLength) &&
                        contentLength > max_response_bytes)
                    {
                        HEAP32[error_kind_out >> 2] = 2;
                        writeString(error_out, "response exceeds max_response_bytes");
                        return 0;
                    }

                    const bytes = new Uint8Array(await response.arrayBuffer());
                    if (max_response_bytes > 0 && bytes.byteLength > max_response_bytes)
                    {
                        HEAP32[error_kind_out >> 2] = 2;
                        writeString(error_out, "response exceeds max_response_bytes");
                        return 0;
                    }

                    let responseHeaders = "";
                    response.headers.forEach((value, name) => {
                        responseHeaders += name + ":" + value + "\n";
                    });

                    HEAP32[status_out >> 2] = response.status;
                    writeString(reason_out, response.statusText || "");
                    writeString(url_out, response.url || UTF8ToString(url_ptr));
                    writeString(headers_out, responseHeaders);
                    if (bytes.byteLength > 0)
                    {
                        const body = _malloc(bytes.byteLength);
                        if (!body)
                        {
                            throw new Error("WebAssembly allocation failed");
                        }
                        HEAPU8.set(bytes, body);
                        HEAPU32[body_out >> 2] = body;
                        HEAPU32[body_size_out >> 2] = bytes.byteLength;
                    }
                    return 1;
                }
                catch (error)
                {
                    const timedOut = error && error.name === "AbortError";
                    HEAP32[error_kind_out >> 2] = timedOut ? 1 : 0;
                    writeString(error_out,
                                timedOut ? "browser Fetch request timed out"
                                         : String(error && error.message ? error.message : error));
                    return 0;
                }
                finally
                {
                    if (timer)
                    {
                        clearTimeout(timer);
                    }
                }
            });

bool EqualsIgnoreCase(std::string_view left, std::string_view right)
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(left[i])));
        const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(right[i])));
        if (a != b)
        {
            return false;
        }
    }
    return true;
}

bool IsBrowserManagedHeader(std::string_view name)
{
    static constexpr std::string_view kManaged[] = {
        "accept-encoding", "connection", "content-length", "cookie", "host",
        "origin",          "referer",    "user-agent",     "via",
    };
    for (const std::string_view managed : kManaged)
    {
        if (EqualsIgnoreCase(name, managed))
        {
            return true;
        }
    }
    return name.size() >= 4 && std::tolower(static_cast<unsigned char>(name[0])) == 's' &&
           std::tolower(static_cast<unsigned char>(name[1])) == 'e' &&
           std::tolower(static_cast<unsigned char>(name[2])) == 'c' && name[3] == '-';
}

std::string SerializeHeaders(const std::map<std::string, std::string> &headers)
{
    std::ostringstream out;
    for (const auto &[name, value] : headers)
    {
        if (!IsBrowserManagedHeader(name))
        {
            out << name << ':' << value << '\n';
        }
    }
    return out.str();
}

std::vector<Header> ParseHeaders(std::string_view text)
{
    std::vector<Header> headers;
    std::size_t start = 0;
    while (start < text.size())
    {
        const std::size_t end = text.find('\n', start);
        const std::string_view line =
            text.substr(start, end == std::string_view::npos ? text.size() - start : end - start);
        const std::size_t separator = line.find(':');
        if (separator != std::string_view::npos && separator > 0)
        {
            headers.push_back(Header{std::string{line.substr(0, separator)},
                                     std::string{line.substr(separator + 1)}});
        }
        if (end == std::string_view::npos)
        {
            break;
        }
        start = end + 1;
    }
    return headers;
}

Result<void> ValidateWebOptions(Method method, const Options &options,
                                const PreparedRequest &prepared)
{
    if (!options.verify_tls)
    {
        return Result<void>::Err(Error{ErrorCode::InvalidArgument,
                                       "browsers do not allow TLS verification to be disabled"});
    }
    if (options.ca_bundle.has_value() || options.client_cert.has_value() ||
        options.client_key.has_value())
    {
        return Result<void>::Err(
            Error{ErrorCode::InvalidArgument,
                  "browser TLS trust and client certificates are controlled by the browser"});
    }
    if (options.proxy.has_value())
    {
        return Result<void>::Err(
            Error{ErrorCode::InvalidArgument, "browsers do not expose per-request HTTP proxies"});
    }
    if (options.response_writer)
    {
        return Result<void>::Err(
            Error{ErrorCode::InvalidArgument,
                  "response_writer is unavailable with the web backend; responses are buffered"});
    }
    if (!options.decompress)
    {
        return Result<void>::Err(
            Error{ErrorCode::InvalidArgument,
                  "browser Fetch always applies browser-managed content decoding"});
    }
    if (options.auth.kind == Auth::Kind::Digest)
    {
        return Result<void>::Err(
            Error{ErrorCode::InvalidArgument, "Digest authentication is unavailable in browsers"});
    }
    if (!options.cookies.empty())
    {
        return Result<void>::Err(
            Error{ErrorCode::InvalidArgument,
                  "Cookie headers are browser-managed; use same-origin browser cookies"});
    }
    if ((method == Method::Get || method == Method::Head) && !prepared.body.empty())
    {
        return Result<void>::Err(
            Error{ErrorCode::InvalidArgument, "browser Fetch does not allow a GET or HEAD body"});
    }
    return Result<void>::Ok();
}

class WebTransport final : public Transport
{
  public:
    [[nodiscard]] std::string_view Name() const noexcept override
    {
        return "web";
    }

    [[nodiscard]] bool AutoPreferred() const noexcept override
    {
        return true;
    }

    [[nodiscard]] Result<Response> Execute(Method method, std::string_view url,
                                           const Options &options) override
    {
        if (request_active_)
        {
            return Result<Response>::Err(
                Error{ErrorCode::InvalidArgument,
                      "the web backend allows one in-flight synchronous request per Wasm module"});
        }

        auto parsed_url = ParseUrl(url);
        if (!parsed_url)
        {
            return Result<Response>::Err(parsed_url.error());
        }

        const PreparedRequest prepared = PrepareRequest(options);
        auto valid = ValidateWebOptions(method, options, prepared);
        if (!valid)
        {
            return Result<Response>::Err(valid.error());
        }

        const std::string request_url = AppendQuery(url, options.params);
        const std::string request_method{ToString(method)};
        const std::string request_headers = SerializeHeaders(prepared.headers);
        const auto started = std::chrono::steady_clock::now();

        int status = 0;
        char *reason = nullptr;
        char *final_url = nullptr;
        char *response_headers = nullptr;
        char *response_body = nullptr;
        std::size_t response_body_size = 0;
        char *error = nullptr;
        int error_kind = 0;

        const auto timeout_count = options.timeout.count();
        const int timeout_ms = timeout_count > static_cast<long long>(INT_MAX)
                                   ? INT_MAX
                                   : static_cast<int>(timeout_count);
        request_active_ = true;
        const int ok = MogBrowserFetch(
            request_url.c_str(), request_method.c_str(), request_headers.c_str(),
            prepared.body.data(), prepared.body.size(), timeout_ms, options.allow_redirects ? 1 : 0,
            options.max_response_bytes, &status, &reason, &final_url, &response_headers,
            &response_body, &response_body_size, &error, &error_kind);
        request_active_ = false;

        if (ok == 0)
        {
            const std::string message =
                error != nullptr ? std::string{error} : std::string{"browser Fetch failed"};
            std::free(error);
            const ErrorCode code = error_kind == 1   ? ErrorCode::Timeout
                                   : error_kind == 2 ? ErrorCode::ResponseTooLarge
                                   : error_kind == 3 ? ErrorCode::ProtocolError
                                                     : ErrorCode::ConnectFailed;
            return Result<Response>::Err(Error{code, message});
        }

        Response response;
        response.status_code = status;
        response.reason = reason != nullptr ? std::string{reason} : std::string{};
        response.url = final_url != nullptr ? std::string{final_url} : request_url;
        response.headers =
            response_headers != nullptr ? ParseHeaders(response_headers) : std::vector<Header>{};
        if (response_body != nullptr)
        {
            response.body.assign(response_body, response_body_size);
        }
        response.downloaded_bytes = response_body_size;
        response.backend = "web";
        response.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);

        std::free(reason);
        std::free(final_url);
        std::free(response_headers);
        std::free(response_body);
        return Result<Response>::Ok(std::move(response));
    }

  private:
    bool request_active_ = false;
};

} // namespace

std::unique_ptr<Transport> MakeWebTransport()
{
    return std::make_unique<WebTransport>();
}

} // namespace mog::detail
