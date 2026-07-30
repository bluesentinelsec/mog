/**
 * @file native_backend.hpp
 * @brief Factory for the platform-native transport (OS-provided HTTP stack).
 */
#pragma once

#include "http/detail/transport.hpp"

#include <memory>

namespace mog::detail
{

/**
 * @brief Construct the platform-native transport, or nullptr when this platform
 *        has no native backend yet.
 *
 * macOS returns an NSURLSession-backed transport; other platforms currently
 * return nullptr (the registry then uses the placeholder until curl/WinHTTP land).
 */
[[nodiscard]] std::unique_ptr<Transport> MakeNativeTransport();

} // namespace mog::detail
