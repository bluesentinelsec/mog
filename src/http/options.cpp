/**
 * @file options.cpp
 * @brief Method parsing helpers.
 */

#include "mog/options.hpp"

#include <cctype>
#include <string>

namespace mog
{
namespace
{

std::string ToLower(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char ch : text)
    {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

} // namespace

std::string_view ToString(Method method) noexcept
{
    switch (method)
    {
    case Method::Get:
        return "GET";
    case Method::Post:
        return "POST";
    case Method::Put:
        return "PUT";
    case Method::Patch:
        return "PATCH";
    case Method::Delete:
        return "DELETE";
    case Method::Head:
        return "HEAD";
    case Method::Options:
        return "OPTIONS";
    }
    return "GET";
}

std::optional<Method> ParseMethod(std::string_view text)
{
    const std::string lower = ToLower(text);
    if (lower == "get")
    {
        return Method::Get;
    }
    if (lower == "post")
    {
        return Method::Post;
    }
    if (lower == "put")
    {
        return Method::Put;
    }
    if (lower == "patch")
    {
        return Method::Patch;
    }
    if (lower == "delete")
    {
        return Method::Delete;
    }
    if (lower == "head")
    {
        return Method::Head;
    }
    if (lower == "options")
    {
        return Method::Options;
    }
    return std::nullopt;
}

} // namespace mog
