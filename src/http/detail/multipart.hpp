/**
 * @file multipart.hpp
 * @brief Build multipart/form-data request bodies (RFC 7578) for the embedded backend.
 */
#pragma once

#include "mog/options.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace mog::detail
{

/**
 * @brief Generate a random, collision-resistant multipart boundary token.
 *
 * The content is not scanned for the boundary (a random token makes a collision
 * astronomically unlikely) — this is a documented simplification.
 */
[[nodiscard]] std::string GenerateMultipartBoundary();

/**
 * @brief Guess a part Content-Type from a filename extension.
 * @return A known type for common extensions, else "application/octet-stream".
 */
[[nodiscard]] std::string GuessContentType(std::string_view filename);

/**
 * @brief Serialize @p parts into a multipart/form-data body using @p boundary.
 *
 * File parts (those with a filename) always get a Content-Type (guessed from the
 * filename when the part's content_type is empty). Plain fields emit a
 * Content-Type only when one was explicitly set. Names/filenames have @c " and
 * CR/LF percent-encoded.
 */
[[nodiscard]] std::string BuildMultipartBody(const std::vector<FormPart> &parts,
                                             const std::string &boundary);

} // namespace mog::detail
