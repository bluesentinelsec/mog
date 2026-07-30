/**
 * @file native_backend_stub.cpp
 * @brief Non-Apple fallback: no native transport yet (curl/WinHTTP land later).
 */

#include "http/detail/native_backend.hpp"

namespace mog::detail
{

std::unique_ptr<Transport> MakeNativeTransport()
{
    return nullptr;
}

} // namespace mog::detail
