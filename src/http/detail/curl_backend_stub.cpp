/**
 * @file curl_backend_stub.cpp
 * @brief Platforms without the dlopen curl driver (Windows) get no curl transport.
 */

#include "http/detail/curl_backend.hpp"

namespace mog::detail
{

std::unique_ptr<Transport> MakeCurlTransport()
{
    return nullptr;
}

} // namespace mog::detail
