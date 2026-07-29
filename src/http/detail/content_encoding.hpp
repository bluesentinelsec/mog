/**
 * @file content_encoding.hpp
 * @brief HTTP Content-Encoding decode (gzip / deflate) for the embedded backend.
 *
 * Uses miniz (static) so the client remains self-contained — no system libz.
 */
#pragma once

#include "mog/error.hpp"
#include "mog/response.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace mog::detail
{

/**
 * @brief Tokenize a Content-Encoding or Accept-Encoding value (comma-separated).
 */
[[nodiscard]] std::vector<std::string> SplitEncodingList(std::string_view value);

/**
 * @brief Decode a body according to Content-Encoding header values.
 *
 * @param body Encoded payload (moved from when successful).
 * @param content_encoding_header Raw Content-Encoding header (may be empty).
 * @param max_decoded_bytes Cap on decoded size (0 = unlimited).
 * @return Decoded body, or @p body unchanged when encoding is identity/empty.
 *
 * Supports @c gzip / @c x-gzip and @c deflate (zlib wrap, then raw DEFLATE fallback).
 * Multiple encodings are applied in reverse order (HTTP content codings).
 */
[[nodiscard]] Result<std::string> DecodeContentEncoding(std::string body,
                                                        std::string_view content_encoding_header,
                                                        std::size_t max_decoded_bytes);

/**
 * @brief After successful decode, remove hop/content-coding headers that no longer apply.
 *
 * Removes Content-Encoding and Content-Length (length is of the encoded entity).
 */
void StripContentCodingHeaders(std::vector<Header> &headers);

/**
 * @brief Case-insensitive content-coding name equality (e.g. "GZip" == "gzip").
 */
[[nodiscard]] bool EncodingEquals(std::string_view a, std::string_view b) noexcept;

} // namespace mog::detail
