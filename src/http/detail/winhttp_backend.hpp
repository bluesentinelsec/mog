/**
 * @file winhttp_backend.hpp
 * @brief Factory for the Windows WinHTTP transport.
 */
#pragma once

#include "http/detail/transport.hpp"

#include <memory>

namespace mog::detail
{

/**
 * @brief Construct the WinHTTP-backed transport on Windows, or nullptr elsewhere.
 *
 * WinHTTP ships with Windows, so the returned transport is always Available()
 * there; other platforms get nullptr (the registry then uses the placeholder).
 */
[[nodiscard]] std::unique_ptr<Transport> MakeWinHttpTransport();

} // namespace mog::detail
