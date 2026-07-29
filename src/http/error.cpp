/**
 * @file error.cpp
 * @brief Error formatting helpers.
 */

#include "mog/error.hpp"

#include <sstream>

namespace mog
{
namespace
{

std::string_view ErrorCodeName(ErrorCode code) noexcept
{
    switch (code)
    {
    case ErrorCode::Ok:
        return "ok";
    case ErrorCode::InvalidUrl:
        return "invalid_url";
    case ErrorCode::InvalidArgument:
        return "invalid_argument";
    case ErrorCode::UnsupportedScheme:
        return "unsupported_scheme";
    case ErrorCode::UnsupportedBackend:
        return "unsupported_backend";
    case ErrorCode::DnsFailed:
        return "dns_failed";
    case ErrorCode::ConnectFailed:
        return "connect_failed";
    case ErrorCode::TlsFailed:
        return "tls_failed";
    case ErrorCode::Timeout:
        return "timeout";
    case ErrorCode::IoError:
        return "io_error";
    case ErrorCode::ProtocolError:
        return "protocol_error";
    case ErrorCode::TooManyRedirects:
        return "too_many_redirects";
    case ErrorCode::HttpStatus:
        return "http_status";
    case ErrorCode::ResponseTooLarge:
        return "response_too_large";
    case ErrorCode::ProxyError:
        return "proxy_error";
    case ErrorCode::FileError:
        return "file_error";
    case ErrorCode::JsonError:
        return "json_error";
    case ErrorCode::CompressionError:
        return "compression_error";
    case ErrorCode::DynamicLibraryError:
        return "dynamic_library_error";
    case ErrorCode::Internal:
        return "internal";
    }
    return "unknown";
}

} // namespace

Error::Error(ErrorCode code, std::string message) : code_(code), message_(std::move(message))
{
}

std::string Error::to_string() const
{
    std::ostringstream oss;
    oss << ErrorCodeName(code_) << ": " << message_;
    return oss.str();
}

std::string_view ToString(ErrorCode code) noexcept
{
    return ErrorCodeName(code);
}

} // namespace mog
