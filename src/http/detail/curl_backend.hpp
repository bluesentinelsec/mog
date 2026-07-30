/**
 * @file curl_backend.hpp
 * @brief Factory for the libcurl transport (loaded at runtime via dlopen).
 */
#pragma once

#include "http/detail/transport.hpp"

#include <memory>

namespace mog::detail
{

/**
 * @brief Construct the libcurl-backed transport, or nullptr on platforms without
 *        a dlopen-based curl driver (e.g. Windows, which uses WinHTTP).
 *
 * The returned transport reports Available() only when libcurl actually loads at
 * runtime; libcurl is never hard-linked.
 */
[[nodiscard]] std::unique_ptr<Transport> MakeCurlTransport();

} // namespace mog::detail
