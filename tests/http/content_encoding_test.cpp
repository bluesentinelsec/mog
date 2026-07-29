/**
 * @file content_encoding_test.cpp
 * @brief Unit tests for gzip/deflate Content-Encoding decode (miniz).
 */

#include "http/detail/content_encoding.hpp"
#include "http/detail/prepare.hpp"
#include "mog/mog.hpp"

#include <cstdint>
#include <gtest/gtest.h>
#include <miniz.h>
#include <string>
#include <vector>

namespace
{

std::string DeflateZlib(std::string_view plain)
{
    mz_ulong bound = mz_compressBound(static_cast<mz_ulong>(plain.size()));
    std::string out(static_cast<std::size_t>(bound), '\0');
    mz_ulong out_len = bound;
    const int rc = mz_compress(reinterpret_cast<unsigned char *>(out.data()), &out_len,
                               reinterpret_cast<const unsigned char *>(plain.data()),
                               static_cast<mz_ulong>(plain.size()));
    EXPECT_EQ(rc, MZ_OK);
    out.resize(static_cast<std::size_t>(out_len));
    return out;
}

std::string DeflateRaw(std::string_view plain)
{
    mz_stream stream{};
    EXPECT_EQ(mz_deflateInit2(&stream, MZ_DEFAULT_COMPRESSION, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS,
                              8, MZ_DEFAULT_STRATEGY),
              MZ_OK);
    stream.next_in = reinterpret_cast<const unsigned char *>(plain.data());
    stream.avail_in = static_cast<unsigned int>(plain.size());

    std::string out;
    out.resize(plain.size() + 64);
    stream.next_out = reinterpret_cast<unsigned char *>(out.data());
    stream.avail_out = static_cast<unsigned int>(out.size());
    EXPECT_EQ(mz_deflate(&stream, MZ_FINISH), MZ_STREAM_END);
    out.resize(static_cast<std::size_t>(stream.total_out));
    mz_deflateEnd(&stream);
    return out;
}

std::string GzipWrap(std::string_view plain)
{
    // Minimal gzip member: header + raw deflate + CRC32 + ISIZE
    const std::string deflated = DeflateRaw(plain);
    std::string out;
    out.push_back(static_cast<char>(0x1f));
    out.push_back(static_cast<char>(0x8b));
    out.push_back(8);                      // CM
    out.push_back(0);                      // FLG
    out.append(4, '\0');                   // MTIME
    out.push_back(0);                      // XFL
    out.push_back(static_cast<char>(255)); // OS unknown
    out.append(deflated);
    const mz_ulong crc =
        mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char *>(plain.data()),
                 static_cast<size_t>(plain.size()));
    const auto isize = static_cast<std::uint32_t>(plain.size());
    for (int i = 0; i < 4; ++i)
    {
        out.push_back(static_cast<char>((crc >> (8 * i)) & 0xffU));
    }
    for (int i = 0; i < 4; ++i)
    {
        out.push_back(static_cast<char>((isize >> (8 * i)) & 0xffU));
    }
    return out;
}

} // namespace

TEST(ContentEncoding, SplitList)
{
    auto parts = mog::detail::SplitEncodingList(" gzip ;q=1.0 , deflate ");
    ASSERT_EQ(parts.size(), 2U);
    EXPECT_EQ(parts[0], "gzip");
    EXPECT_EQ(parts[1], "deflate");
}

TEST(ContentEncoding, DecodeGzip)
{
    const std::string plain = "hello gzip world";
    auto decoded = mog::detail::DecodeContentEncoding(GzipWrap(plain), "gzip", /*max*/ 0);
    ASSERT_TRUE(decoded) << decoded.error().to_string();
    EXPECT_EQ(*decoded, plain);
}

TEST(ContentEncoding, DecodeDeflateZlib)
{
    const std::string plain = "zlib-wrapped-deflate-payload";
    auto decoded = mog::detail::DecodeContentEncoding(DeflateZlib(plain), "deflate", 0);
    ASSERT_TRUE(decoded) << decoded.error().to_string();
    EXPECT_EQ(*decoded, plain);
}

TEST(ContentEncoding, DecodeDeflateRawFallback)
{
    const std::string plain = "raw-deflate-payload";
    auto decoded = mog::detail::DecodeContentEncoding(DeflateRaw(plain), "deflate", 0);
    ASSERT_TRUE(decoded) << decoded.error().to_string();
    EXPECT_EQ(*decoded, plain);
}

TEST(ContentEncoding, IdentityNoOp)
{
    auto decoded = mog::detail::DecodeContentEncoding("plain", "identity", 0);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(*decoded, "plain");
}

TEST(ContentEncoding, UnsupportedFails)
{
    auto decoded = mog::detail::DecodeContentEncoding("x", "br", 0);
    ASSERT_FALSE(decoded);
    EXPECT_EQ(decoded.error().code(), mog::ErrorCode::CompressionError);
}

TEST(ContentEncoding, MaxDecodedBytes)
{
    const std::string plain(1000, 'a');
    auto decoded = mog::detail::DecodeContentEncoding(GzipWrap(plain), "gzip", /*max*/ 100);
    ASSERT_FALSE(decoded);
    EXPECT_EQ(decoded.error().code(), mog::ErrorCode::ResponseTooLarge);
}

TEST(ContentEncoding, StripHeaders)
{
    std::vector<mog::Header> headers{
        {"Content-Encoding", "gzip"}, {"Content-Length", "12"}, {"Content-Type", "text/plain"}};
    mog::detail::StripContentCodingHeaders(headers);
    ASSERT_EQ(headers.size(), 1U);
    EXPECT_EQ(headers[0].name, "Content-Type");
}

TEST(ContentEncodingPrepare, AcceptEncodingWhenDecompressEnabled)
{
    mog::Options opt;
    auto wire = mog::detail::PrepareRequest(opt);
    auto it = wire.headers.find("Accept-Encoding");
    ASSERT_NE(it, wire.headers.end());
    EXPECT_NE(it->second.find("gzip"), std::string::npos);
    EXPECT_NE(it->second.find("deflate"), std::string::npos);
}

TEST(ContentEncodingPrepare, NoAcceptEncodingWhenDisabled)
{
    mog::Options opt;
    opt.decompress = false;
    auto wire = mog::detail::PrepareRequest(opt);
    EXPECT_EQ(wire.headers.count("Accept-Encoding"), 0U);
}

TEST(ContentEncodingPrepare, RespectsCallerAcceptEncoding)
{
    mog::Options opt;
    opt.headers["Accept-Encoding"] = "identity";
    auto wire = mog::detail::PrepareRequest(opt);
    EXPECT_EQ(wire.headers["Accept-Encoding"], "identity");
}
