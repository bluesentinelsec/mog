/**
 * @file server_web.cpp
 * @brief Browser-safe server API implementation for Emscripten.
 */

#include "mog/server.hpp"

#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace mog
{
namespace
{

bool EqualsIgnoreCase(std::string_view left, std::string_view right)
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(left[i])) !=
            std::tolower(static_cast<unsigned char>(right[i])))
        {
            return false;
        }
    }
    return true;
}

std::string MimeType(const std::string &path)
{
    const auto dot = path.find_last_of('.');
    std::string extension = dot == std::string::npos ? std::string{} : path.substr(dot + 1);
    for (char &ch : extension)
    {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    static const std::map<std::string, std::string> kTypes = {
        {"css", "text/css; charset=utf-8"},
        {"html", "text/html; charset=utf-8"},
        {"js", "text/javascript; charset=utf-8"},
        {"json", "application/json"},
        {"png", "image/png"},
        {"svg", "image/svg+xml"},
        {"txt", "text/plain; charset=utf-8"},
        {"wasm", "application/wasm"},
    };
    const auto it = kTypes.find(extension);
    return it == kTypes.end() ? "application/octet-stream" : it->second;
}

Error BrowserServerError()
{
    return Error{ErrorCode::UnsupportedBackend,
                 "mog::Server is unavailable in browser WebAssembly; host HTTP on a native "
                 "process and use the web backend as a client"};
}

} // namespace

std::string ServerRequest::header(std::string_view name) const
{
    for (const auto &field : headers)
    {
        if (EqualsIgnoreCase(field.name, name))
        {
            return field.value;
        }
    }
    return {};
}

ServerResponse &ServerResponse::set_header(std::string name, std::string value)
{
    for (auto &field : headers)
    {
        if (EqualsIgnoreCase(field.name, name))
        {
            field.value = std::move(value);
            return *this;
        }
    }
    headers.push_back(Header{std::move(name), std::move(value)});
    return *this;
}

std::string ServerResponse::header(std::string_view name) const
{
    for (const auto &field : headers)
    {
        if (EqualsIgnoreCase(field.name, name))
        {
            return field.value;
        }
    }
    return {};
}

ServerResponse ServerResponse::Text(int status, std::string body, std::string content_type)
{
    ServerResponse response;
    response.status_code = status;
    response.body = std::move(body);
    response.set_header("Content-Type", std::move(content_type));
    return response;
}

ServerResponse ServerResponse::Json(int status, std::string json)
{
    return Text(status, std::move(json), "application/json");
}

ServerResponse ServerResponse::Status(int status)
{
    ServerResponse response;
    response.status_code = status;
    return response;
}

ServerResponse ServerResponse::NotFound(std::string body)
{
    return Text(404, std::move(body));
}

ServerResponse ServerResponse::Redirect(int status, std::string location)
{
    ServerResponse response;
    response.status_code = status;
    response.set_header("Location", std::move(location));
    return response;
}

Result<ServerResponse> ServerResponse::File(const std::string &path)
{
    std::error_code error;
    if (!fs::is_regular_file(path, error))
    {
        return Result<ServerResponse>::Err(Error{ErrorCode::FileError, "not a file: " + path});
    }
    const std::uintmax_t size = fs::file_size(path, error);
    if (error)
    {
        return Result<ServerResponse>::Err(Error{ErrorCode::FileError, "cannot stat: " + path});
    }

    ServerResponse response;
    response.set_header("Content-Type", MimeType(path));
    response.set_header("Content-Length", std::to_string(size));
    response.body_producer = [path](const ResponseSink &sink) -> Result<void> {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            return Result<void>::Err(Error{ErrorCode::FileError, "cannot open: " + path});
        }
        std::array<char, 64 * 1024> bytes{};
        while (input)
        {
            input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            const std::streamsize count = input.gcount();
            if (count > 0)
            {
                auto written =
                    sink(std::string_view{bytes.data(), static_cast<std::size_t>(count)});
                if (!written)
                {
                    return written;
                }
            }
        }
        return Result<void>::Ok();
    };
    return Result<ServerResponse>::Ok(std::move(response));
}

Result<TlsServerConfig> TlsServerConfig::FromFiles(const std::string &, const std::string &,
                                                   const std::string &)
{
    return Result<TlsServerConfig>::Err(BrowserServerError());
}

Result<TlsServerConfig> TlsServerConfig::SelfSigned(const std::string &)
{
    return Result<TlsServerConfig>::Err(BrowserServerError());
}

struct Server::Impl
{
    explicit Impl(ServerOptions value) : options(std::move(value))
    {
    }

    ServerOptions options;
    std::map<std::string, Handler> routes;
    std::vector<std::pair<std::string, std::string>> mounts;
    Handler default_handler;
};

Server::Server(ServerOptions options) : impl_(std::make_unique<Impl>(std::move(options)))
{
}

Server::~Server() = default;
Server::Server(Server &&) noexcept = default;
Server &Server::operator=(Server &&) noexcept = default;

Server &Server::route(Method method, std::string path, Handler handler)
{
    impl_->routes[std::string{ToString(method)} + " " + path] = std::move(handler);
    return *this;
}

Server &Server::serve_files(std::string mount_prefix, std::string directory, StaticOptions)
{
    impl_->mounts.emplace_back(std::move(mount_prefix), std::move(directory));
    return *this;
}

Server &Server::set_default_handler(Handler handler)
{
    impl_->default_handler = std::move(handler);
    return *this;
}

Result<void> Server::start()
{
    return Result<void>::Err(BrowserServerError());
}

void Server::stop() noexcept
{
}

void Server::wait()
{
}

bool Server::running() const noexcept
{
    return false;
}

std::uint16_t Server::port() const noexcept
{
    return 0;
}

} // namespace mog
