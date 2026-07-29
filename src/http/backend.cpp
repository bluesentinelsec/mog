/**
 * @file backend.cpp
 * @brief Backend name parsing and resolution (CLI / env / default).
 */

#include "mog/backend.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
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

std::optional<Backend> ParseBackend(std::string_view text)
{
    const std::string lower = ToLower(text);
    if (lower == "auto")
    {
        return Backend::Auto;
    }
    if (lower == "embedded" || lower == "fallback" || lower == "internal")
    {
        return Backend::Embedded;
    }
    if (lower == "curl" || lower == "libcurl")
    {
        return Backend::Curl;
    }
    if (lower == "winhttp" || lower == "win")
    {
        return Backend::WinHttp;
    }
    if (lower == "native" || lower == "os" || lower == "system")
    {
        return Backend::Native;
    }
    return std::nullopt;
}

std::string_view ToString(Backend backend) noexcept
{
    switch (backend)
    {
    case Backend::Auto:
        return "auto";
    case Backend::Embedded:
        return "embedded";
    case Backend::Curl:
        return "curl";
    case Backend::WinHttp:
        return "winhttp";
    case Backend::Native:
        return "native";
    }
    return "unknown";
}

std::optional<Backend> BackendFromEnvironment()
{
    const char *value = std::getenv("MOG_BACKEND");
    if (value == nullptr || value[0] == '\0')
    {
        return std::nullopt;
    }
    return ParseBackend(value);
}

Backend ResolveBackend(std::optional<Backend> explicit_override)
{
    if (explicit_override.has_value() && *explicit_override != Backend::Auto)
    {
        return *explicit_override;
    }

    if (explicit_override.has_value() && *explicit_override == Backend::Auto)
    {
        // Explicit Auto still allows env override, then default.
        if (auto env = BackendFromEnvironment(); env.has_value() && *env != Backend::Auto)
        {
            return *env;
        }
        return Backend::Embedded;
    }

    // No explicit override: env then default.
    if (auto env = BackendFromEnvironment(); env.has_value())
    {
        if (*env == Backend::Auto)
        {
            return Backend::Embedded;
        }
        return *env;
    }

    return Backend::Embedded;
}

} // namespace mog
