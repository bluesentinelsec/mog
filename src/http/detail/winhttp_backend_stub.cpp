/**
 * @file winhttp_backend_stub.cpp
 * @brief Non-Windows platforms have no WinHTTP transport.
 */

#include "http/detail/winhttp_backend.hpp"

namespace mog::detail
{

std::unique_ptr<Transport> MakeWinHttpTransport()
{
    return nullptr;
}

} // namespace mog::detail
