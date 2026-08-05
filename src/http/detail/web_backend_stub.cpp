/**
 * @file web_backend_stub.cpp
 * @brief Non-Emscripten stub for the browser Fetch transport.
 */

#include "http/detail/web_backend.hpp"

namespace mog::detail
{

std::unique_ptr<Transport> MakeWebTransport()
{
    return nullptr;
}

} // namespace mog::detail
