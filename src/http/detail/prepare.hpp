/**
 * @file prepare.hpp
 * @brief Normalize Options into wire headers + body for the embedded backend.
 */
#pragma once

#include "mog/options.hpp"

#include <map>
#include <string>

namespace mog::detail
{

struct PreparedRequest
{
    std::map<std::string, std::string> headers;
    std::string body;
};

/**
 * @brief Apply json/form/auth/cookies/user-agent defaults onto headers and body.
 */
[[nodiscard]] PreparedRequest PrepareRequest(const Options &options);

/**
 * @return Effective connect timeout.
 */
[[nodiscard]] std::chrono::milliseconds ConnectTimeout(const Options &options) noexcept;

/**
 * @return Effective I/O timeout for send/recv.
 */
[[nodiscard]] std::chrono::milliseconds IoTimeout(const Options &options) noexcept;

} // namespace mog::detail
