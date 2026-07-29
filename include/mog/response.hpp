/**
 * @file response.hpp
 * @brief HTTP response value type.
 */
#pragma once

#include "mog/error.hpp"

#include <map>
#include <string>
#include <string_view>

namespace mog
{

/**
 * @brief Successful HTTP exchange result (status, headers, body).
 */
class Response
{
  public:
    Response() = default;

    int status_code = 0;
    std::string reason;
    std::string url;
    /// Header names stored as returned by the server (lookup is case-insensitive).
    std::map<std::string, std::string> headers;
    std::string body;
    /// Number of redirects followed to produce this response.
    int history_len = 0;
    /// Concrete backend that served the request.
    std::string backend;

    [[nodiscard]] bool ok() const noexcept
    {
        return status_code >= 200 && status_code < 400;
    }

    /**
     * @return Response body as text (same as @ref body for now).
     */
    [[nodiscard]] const std::string &text() const noexcept
    {
        return body;
    }

    /**
     * @return Response body bytes (alias of @ref body).
     */
    [[nodiscard]] const std::string &content() const noexcept
    {
        return body;
    }

    /**
     * @brief Case-insensitive header lookup.
     * @return Header value or empty string if missing.
     */
    [[nodiscard]] std::string header(std::string_view name) const;

    /**
     * @brief Throw-style check: returns Error when status is 4xx/5xx.
     *
     * Does not throw; returns an error Result for library consistency.
     */
    [[nodiscard]] Result<void> raise_for_status() const;
};

} // namespace mog
