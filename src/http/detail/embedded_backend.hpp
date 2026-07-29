/**
 * @file embedded_backend.hpp
 * @brief Built-in HTTP/1.1 + mbedTLS backend.
 */
#pragma once

#include "mog/options.hpp"
#include "mog/response.hpp"

#include <string_view>

namespace mog::detail
{

/**
 * @brief Execute one logical HTTP request (including redirects) on the embedded stack.
 */
[[nodiscard]] Result<Response> EmbeddedRequest(Method method, std::string_view url,
                                               const Options &options);

} // namespace mog::detail
