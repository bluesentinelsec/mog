/**
 * @file env.hpp
 * @brief Portable environment variable reads (MSVC-safe).
 */
#pragma once

#include <cstdlib>
#include <optional>
#include <string>

#if defined(_WIN32)
#include <stdlib.h>
#endif

namespace mog::detail
{

/**
 * @brief Read a non-empty environment variable.
 * @return Value, or nullopt if unset/empty.
 */
inline std::optional<std::string> GetEnv(const char *name)
{
#if defined(_WIN32)
    char *buffer = nullptr;
    std::size_t len = 0;
    if (_dupenv_s(&buffer, &len, name) != 0 || buffer == nullptr)
    {
        return std::nullopt;
    }
    std::string value{buffer};
    free(buffer);
    if (value.empty())
    {
        return std::nullopt;
    }
    return value;
#else
    const char *value = std::getenv(name);
    if (value == nullptr || value[0] == '\0')
    {
        return std::nullopt;
    }
    return std::string{value};
#endif
}

} // namespace mog::detail
