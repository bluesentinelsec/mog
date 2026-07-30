/**
 * @file multipart.cpp
 * @brief Build multipart/form-data request bodies (RFC 7578).
 */

#include "http/detail/multipart.hpp"

#include <array>
#include <cctype>
#include <random>
#include <string>

namespace mog::detail
{
namespace
{

std::string ToLowerExt(std::string_view filename)
{
    const auto dot = filename.find_last_of('.');
    if (dot == std::string_view::npos)
    {
        return {};
    }
    std::string ext;
    for (std::size_t i = dot + 1; i < filename.size(); ++i)
    {
        ext.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(filename[i]))));
    }
    return ext;
}

// Percent-encode the characters not permitted (unescaped) in a quoted-string
// parameter value: double-quote and CR/LF. Everything else passes through.
std::string EscapeHeaderParam(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char ch : text)
    {
        switch (ch)
        {
        case '"':
            out += "%22";
            break;
        case '\r':
            out += "%0D";
            break;
        case '\n':
            out += "%0A";
            break;
        default:
            out.push_back(ch);
            break;
        }
    }
    return out;
}

} // namespace

std::string GenerateMultipartBoundary()
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<int> dist(0, 15);
    std::string boundary = "----mogFormBoundary";
    for (int i = 0; i < 24; ++i)
    {
        boundary.push_back(kHex[dist(gen)]);
    }
    return boundary;
}

std::string GuessContentType(std::string_view filename)
{
    const std::string ext = ToLowerExt(filename);
    static const std::array<std::pair<std::string_view, std::string_view>, 16> kTypes = {{
        {"txt", "text/plain"},
        {"csv", "text/csv"},
        {"html", "text/html"},
        {"htm", "text/html"},
        {"css", "text/css"},
        {"json", "application/json"},
        {"xml", "application/xml"},
        {"js", "application/javascript"},
        {"pdf", "application/pdf"},
        {"zip", "application/zip"},
        {"gz", "application/gzip"},
        {"png", "image/png"},
        {"jpg", "image/jpeg"},
        {"jpeg", "image/jpeg"},
        {"gif", "image/gif"},
        {"svg", "image/svg+xml"},
    }};
    for (const auto &[e, ct] : kTypes)
    {
        if (ext == e)
        {
            return std::string{ct};
        }
    }
    return "application/octet-stream";
}

std::string BuildMultipartBody(const std::vector<FormPart> &parts, const std::string &boundary)
{
    std::string body;
    for (const auto &part : parts)
    {
        body += "--";
        body += boundary;
        body += "\r\n";

        body += "Content-Disposition: form-data; name=\"";
        body += EscapeHeaderParam(part.name);
        body += "\"";
        if (part.filename.has_value())
        {
            body += "; filename=\"";
            body += EscapeHeaderParam(*part.filename);
            body += "\"";
        }
        body += "\r\n";

        if (part.filename.has_value())
        {
            // File parts always carry a Content-Type (guessed when unset).
            body += "Content-Type: ";
            body +=
                part.content_type.empty() ? GuessContentType(*part.filename) : part.content_type;
            body += "\r\n";
        }
        else if (!part.content_type.empty())
        {
            body += "Content-Type: ";
            body += part.content_type;
            body += "\r\n";
        }

        body += "\r\n";
        body += part.value;
        body += "\r\n";
    }
    body += "--";
    body += boundary;
    body += "--\r\n";
    return body;
}

} // namespace mog::detail
