/**
 * @file url.hpp
 * @brief Minimal URL parsing for the embedded client.
 */
#pragma once

#include "mog/error.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace mog::detail
{

struct Url
{
    std::string scheme; // http / https
    std::string host;
    std::uint16_t port = 0;
    std::string path;  // begins with /
    std::string query; // without leading ?
};

[[nodiscard]] Result<Url> ParseUrl(std::string_view input);

[[nodiscard]] std::string BuildUrl(const Url &url);

[[nodiscard]] std::string JoinUrl(std::string_view base, std::string_view ref);

[[nodiscard]] std::string EncodeQuery(const std::map<std::string, std::string> &params);

[[nodiscard]] std::string AppendQuery(std::string_view url,
                                      const std::map<std::string, std::string> &params);

[[nodiscard]] bool IsDefaultPort(const Url &url) noexcept;

} // namespace mog::detail
