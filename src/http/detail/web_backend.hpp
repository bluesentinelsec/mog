/**
 * @file web_backend.hpp
 * @brief Browser Fetch transport factory.
 */
#pragma once

#include "http/detail/transport.hpp"

#include <memory>

namespace mog::detail
{

/**
 * @brief Create the Emscripten browser Fetch transport.
 * @return A transport on Emscripten, or nullptr on other platforms.
 */
[[nodiscard]] std::unique_ptr<Transport> MakeWebTransport();

} // namespace mog::detail
