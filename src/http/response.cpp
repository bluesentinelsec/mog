/**
 * @file response.cpp
 * @brief Response helpers.
 */

#include "mog/response.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace mog
{
namespace
{

bool EqualsIgnoreCase(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb))
        {
            return false;
        }
    }
    return true;
}

} // namespace

std::string Response::header(std::string_view name) const
{
    for (const auto &entry : headers)
    {
        if (EqualsIgnoreCase(entry.first, name))
        {
            return entry.second;
        }
    }
    return {};
}

Result<void> Response::raise_for_status() const
{
    if (status_code >= 400)
    {
        std::ostringstream oss;
        oss << status_code;
        if (!reason.empty())
        {
            oss << " " << reason;
        }
        oss << " for url: " << url;
        return Result<void>::Err(Error{ErrorCode::HttpStatus, oss.str()});
    }
    return Result<void>::Ok();
}

} // namespace mog
