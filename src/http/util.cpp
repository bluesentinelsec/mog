/**
 * @file util.cpp
 * @brief Encoding helpers and file reads.
 */

#include "mog/util.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <vector>

namespace mog
{
namespace
{

bool IsUnreserved(char ch)
{
    return (std::isalnum(static_cast<unsigned char>(ch)) != 0) || ch == '-' || ch == '_' ||
           ch == '.' || ch == '~';
}

} // namespace

std::string UrlEncode(std::string_view text)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size() * 3);
    for (const char ch : text)
    {
        if (IsUnreserved(ch))
        {
            out.push_back(ch);
        }
        else if (ch == ' ')
        {
            // form-urlencoded commonly uses + for space; also accept %20.
            out.push_back('+');
        }
        else
        {
            const auto uch = static_cast<unsigned char>(ch);
            out.push_back('%');
            out.push_back(kHex[(uch >> 4U) & 0x0FU]);
            out.push_back(kHex[uch & 0x0FU]);
        }
    }
    return out;
}

std::string EncodeForm(const std::map<std::string, std::string> &fields)
{
    std::ostringstream oss;
    bool first = true;
    for (const auto &entry : fields)
    {
        if (!first)
        {
            oss << '&';
        }
        first = false;
        oss << UrlEncode(entry.first) << '=' << UrlEncode(entry.second);
    }
    return oss.str();
}

std::string EncodeCookieHeader(const std::map<std::string, std::string> &cookies)
{
    std::ostringstream oss;
    bool first = true;
    for (const auto &entry : cookies)
    {
        if (!first)
        {
            oss << "; ";
        }
        first = false;
        oss << entry.first << '=' << entry.second;
    }
    return oss.str();
}

bool ParseSetCookie(std::string_view set_cookie, std::string &name, std::string &value)
{
    const auto semi = set_cookie.find(';');
    const std::string_view nv =
        semi == std::string_view::npos ? set_cookie : set_cookie.substr(0, semi);
    const auto eq = nv.find('=');
    if (eq == std::string_view::npos || eq == 0)
    {
        return false;
    }
    name = std::string{nv.substr(0, eq)};
    value = std::string{nv.substr(eq + 1)};
    // trim spaces on name
    while (!name.empty() && name.front() == ' ')
    {
        name.erase(name.begin());
    }
    return !name.empty();
}

std::string Base64Encode(std::string_view data)
{
    static constexpr char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    std::size_t i = 0;
    while (i + 2 < data.size())
    {
        const auto a = static_cast<unsigned char>(data[i]);
        const auto b = static_cast<unsigned char>(data[i + 1]);
        const auto c = static_cast<unsigned char>(data[i + 2]);
        out.push_back(kTable[(a >> 2) & 0x3F]);
        out.push_back(kTable[((a & 0x03) << 4) | ((b >> 4) & 0x0F)]);
        out.push_back(kTable[((b & 0x0F) << 2) | ((c >> 6) & 0x03)]);
        out.push_back(kTable[c & 0x3F]);
        i += 3;
    }
    if (i < data.size())
    {
        const auto a = static_cast<unsigned char>(data[i]);
        out.push_back(kTable[(a >> 2) & 0x3F]);
        if (i + 1 < data.size())
        {
            const auto b = static_cast<unsigned char>(data[i + 1]);
            out.push_back(kTable[((a & 0x03) << 4) | ((b >> 4) & 0x0F)]);
            out.push_back(kTable[((b & 0x0F) << 2)]);
            out.push_back('=');
        }
        else
        {
            out.push_back(kTable[((a & 0x03) << 4)]);
            out.push_back('=');
            out.push_back('=');
        }
    }
    return out;
}

Result<std::string> ReadFile(std::string_view path)
{
    const std::string p{path};
    std::ifstream in(p, std::ios::binary);
    if (!in)
    {
        return Result<std::string>::Err(Error{ErrorCode::FileError, "failed to open file: " + p});
    }
    std::ostringstream oss;
    oss << in.rdbuf();
    if (!in.good() && !in.eof())
    {
        return Result<std::string>::Err(Error{ErrorCode::FileError, "failed to read file: " + p});
    }
    return Result<std::string>::Ok(oss.str());
}

} // namespace mog
