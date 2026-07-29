/**
 * @file content_encoding.cpp
 * @brief gzip/deflate inflate via miniz (static-link friendly).
 */

#include "http/detail/content_encoding.hpp"

#include "mog/log.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <miniz.h>
#include <vector>

namespace mog::detail
{
namespace
{

bool IsSpace(unsigned char ch) noexcept
{
    return ch == ' ' || ch == '\t';
}

/**
 * @brief Skip RFC 1952 gzip member header; return offset of compressed payload.
 *
 * Does not validate optional CRC16 header checksum (rarely used).
 */
Result<std::size_t> GzipPayloadOffset(std::string_view gzip)
{
    if (gzip.size() < 10)
    {
        return Result<std::size_t>::Err(
            Error{ErrorCode::CompressionError, "gzip body too short for header"});
    }
    const auto *bytes = reinterpret_cast<const unsigned char *>(gzip.data());
    if (bytes[0] != 0x1f || bytes[1] != 0x8b)
    {
        return Result<std::size_t>::Err(Error{ErrorCode::CompressionError, "gzip magic mismatch"});
    }
    if (bytes[2] != 8)
    {
        return Result<std::size_t>::Err(
            Error{ErrorCode::CompressionError, "unsupported gzip compression method"});
    }
    const unsigned char flg = bytes[3];
    std::size_t pos = 10;
    // FEXTRA
    if ((flg & 0x04U) != 0)
    {
        if (pos + 2 > gzip.size())
        {
            return Result<std::size_t>::Err(
                Error{ErrorCode::CompressionError, "truncated gzip FEXTRA"});
        }
        const std::size_t xlen =
            static_cast<std::size_t>(bytes[pos]) | (static_cast<std::size_t>(bytes[pos + 1]) << 8U);
        pos += 2;
        if (pos + xlen > gzip.size())
        {
            return Result<std::size_t>::Err(
                Error{ErrorCode::CompressionError, "truncated gzip FEXTRA payload"});
        }
        pos += xlen;
    }
    // FNAME
    if ((flg & 0x08U) != 0)
    {
        while (pos < gzip.size() && bytes[pos] != 0)
        {
            ++pos;
        }
        if (pos >= gzip.size())
        {
            return Result<std::size_t>::Err(
                Error{ErrorCode::CompressionError, "truncated gzip FNAME"});
        }
        ++pos;
    }
    // FCOMMENT
    if ((flg & 0x10U) != 0)
    {
        while (pos < gzip.size() && bytes[pos] != 0)
        {
            ++pos;
        }
        if (pos >= gzip.size())
        {
            return Result<std::size_t>::Err(
                Error{ErrorCode::CompressionError, "truncated gzip FCOMMENT"});
        }
        ++pos;
    }
    // FHCRC
    if ((flg & 0x02U) != 0)
    {
        if (pos + 2 > gzip.size())
        {
            return Result<std::size_t>::Err(
                Error{ErrorCode::CompressionError, "truncated gzip FHCRC"});
        }
        pos += 2;
    }
    // Trailer is 8 bytes (CRC32 + ISIZE); need at least that after payload.
    if (pos + 8 > gzip.size())
    {
        return Result<std::size_t>::Err(
            Error{ErrorCode::CompressionError, "gzip body too short for payload/trailer"});
    }
    return Result<std::size_t>::Ok(pos);
}

/**
 * @brief Inflate DEFLATE data with miniz (zlib wrap or raw).
 * @param window_bits MZ_DEFAULT_WINDOW_BITS for zlib, -MZ_DEFAULT_WINDOW_BITS for raw.
 */
Result<std::string> InflateWithWindowBits(std::string_view compressed, int window_bits,
                                          std::size_t max_decoded_bytes)
{
    if (compressed.empty())
    {
        return Result<std::string>::Ok(std::string{});
    }

    mz_stream stream{};
    const int init_rc = mz_inflateInit2(&stream, window_bits);
    if (init_rc != MZ_OK)
    {
        return Result<std::string>::Err(
            Error{ErrorCode::CompressionError, "mz_inflateInit2 failed"});
    }

    stream.next_in = reinterpret_cast<const unsigned char *>(compressed.data());
    stream.avail_in = static_cast<unsigned int>(
        std::min(compressed.size(), static_cast<std::size_t>(0xffffffffU)));

    std::string out;
    out.reserve(std::min(compressed.size() * 2, static_cast<std::size_t>(64 * 1024)));

    constexpr std::size_t kChunk = 32 * 1024;
    std::vector<unsigned char> buf(kChunk);
    int rc = MZ_OK;
    while (rc != MZ_STREAM_END)
    {
        if (max_decoded_bytes > 0 && out.size() >= max_decoded_bytes)
        {
            mz_inflateEnd(&stream);
            return Result<std::string>::Err(
                Error{ErrorCode::ResponseTooLarge, "decoded body exceeds max_response_bytes"});
        }

        stream.next_out = buf.data();
        stream.avail_out = static_cast<unsigned int>(buf.size());
        rc = mz_inflate(&stream, MZ_NO_FLUSH);
        if (rc != MZ_OK && rc != MZ_STREAM_END)
        {
            mz_inflateEnd(&stream);
            return Result<std::string>::Err(
                Error{ErrorCode::CompressionError,
                      std::string("inflate failed (mz status ") + std::to_string(rc) + ")"});
        }

        const std::size_t produced = buf.size() - static_cast<std::size_t>(stream.avail_out);
        if (max_decoded_bytes > 0 && out.size() + produced > max_decoded_bytes)
        {
            mz_inflateEnd(&stream);
            return Result<std::string>::Err(
                Error{ErrorCode::ResponseTooLarge, "decoded body exceeds max_response_bytes"});
        }
        out.append(reinterpret_cast<const char *>(buf.data()), produced);

        if (rc == MZ_BUF_ERROR && stream.avail_in == 0)
        {
            mz_inflateEnd(&stream);
            return Result<std::string>::Err(
                Error{ErrorCode::CompressionError, "inflate incomplete (truncated input)"});
        }
    }

    mz_inflateEnd(&stream);
    return Result<std::string>::Ok(std::move(out));
}

Result<std::string> DecodeGzip(std::string_view body, std::size_t max_decoded_bytes)
{
    auto offset = GzipPayloadOffset(body);
    if (!offset)
    {
        return Result<std::string>::Err(offset.error());
    }
    // Exclude 8-byte CRC32+ISIZE trailer.
    if (body.size() < *offset + 8)
    {
        return Result<std::string>::Err(
            Error{ErrorCode::CompressionError, "gzip truncated (no trailer)"});
    }
    const std::string_view deflate_payload = body.substr(*offset, body.size() - *offset - 8);
    return InflateWithWindowBits(deflate_payload, -MZ_DEFAULT_WINDOW_BITS, max_decoded_bytes);
}

Result<std::string> DecodeDeflate(std::string_view body, std::size_t max_decoded_bytes)
{
    // HTTP "deflate" is typically zlib-wrapped (RFC 1950). Some servers send raw DEFLATE.
    auto zlib = InflateWithWindowBits(body, MZ_DEFAULT_WINDOW_BITS, max_decoded_bytes);
    if (zlib)
    {
        return zlib;
    }
    MOG_LOG_DEBUG("content-encoding: zlib deflate failed ({}); trying raw DEFLATE",
                  zlib.error().message());
    return InflateWithWindowBits(body, -MZ_DEFAULT_WINDOW_BITS, max_decoded_bytes);
}

Result<std::string> DecodeOne(std::string_view coding, std::string body,
                              std::size_t max_decoded_bytes)
{
    if (EncodingEquals(coding, "identity") || coding.empty())
    {
        return Result<std::string>::Ok(std::move(body));
    }
    if (EncodingEquals(coding, "gzip") || EncodingEquals(coding, "x-gzip"))
    {
        return DecodeGzip(body, max_decoded_bytes);
    }
    if (EncodingEquals(coding, "deflate"))
    {
        return DecodeDeflate(body, max_decoded_bytes);
    }
    return Result<std::string>::Err(
        Error{ErrorCode::CompressionError, "unsupported Content-Encoding: " + std::string{coding}});
}

} // namespace

bool EncodingEquals(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
        {
            return false;
        }
    }
    return true;
}

std::vector<std::string> SplitEncodingList(std::string_view value)
{
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < value.size())
    {
        while (i < value.size() && IsSpace(static_cast<unsigned char>(value[i])))
        {
            ++i;
        }
        if (i >= value.size())
        {
            break;
        }
        const std::size_t start = i;
        while (i < value.size() && value[i] != ',')
        {
            ++i;
        }
        std::size_t end = i;
        while (end > start && IsSpace(static_cast<unsigned char>(value[end - 1])))
        {
            --end;
        }
        // Strip q-value parameters: "gzip;q=1.0" → "gzip"
        std::string token{value.substr(start, end - start)};
        const auto semi = token.find(';');
        if (semi != std::string::npos)
        {
            token.resize(semi);
            while (!token.empty() && IsSpace(static_cast<unsigned char>(token.back())))
            {
                token.pop_back();
            }
        }
        if (!token.empty())
        {
            out.push_back(std::move(token));
        }
        if (i < value.size() && value[i] == ',')
        {
            ++i;
        }
    }
    return out;
}

Result<std::string> DecodeContentEncoding(std::string body,
                                          std::string_view content_encoding_header,
                                          std::size_t max_decoded_bytes)
{
    auto codings = SplitEncodingList(content_encoding_header);
    if (codings.empty())
    {
        return Result<std::string>::Ok(std::move(body));
    }

    // Content codings are applied in order by the sender; decode in reverse.
    for (auto it = codings.rbegin(); it != codings.rend(); ++it)
    {
        auto decoded = DecodeOne(*it, std::move(body), max_decoded_bytes);
        if (!decoded)
        {
            return decoded;
        }
        body = std::move(*decoded);
        MOG_LOG_DEBUG("content-encoding: decoded '{}' → {} bytes", *it, body.size());
    }
    return Result<std::string>::Ok(std::move(body));
}

void StripContentCodingHeaders(std::vector<Header> &headers)
{
    headers.erase(std::remove_if(headers.begin(), headers.end(),
                                 [](const Header &h) {
                                     return EncodingEquals(h.name, "Content-Encoding") ||
                                            EncodingEquals(h.name, "Content-Length");
                                 }),
                  headers.end());
}

} // namespace mog::detail
