/**
 * @file client_test.cpp
 * @brief Integration tests against an in-process HTTP/1.1 server.
 */

#include "mog/mog.hpp"
#include "test_support/local_http_server.hpp"

#include <cstdint>
#include <gtest/gtest.h>
#include <map>
#include <miniz.h>
#include <string>
#include <string_view>
#include <vector>

using mog::test::LocalHttpServer;

std::string MakeGzipBody(std::string_view plain)
{
    mz_stream stream{};
    EXPECT_EQ(mz_deflateInit2(&stream, MZ_DEFAULT_COMPRESSION, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS,
                              8, MZ_DEFAULT_STRATEGY),
              MZ_OK);
    stream.next_in = reinterpret_cast<const unsigned char *>(plain.data());
    stream.avail_in = static_cast<unsigned int>(plain.size());
    std::string deflated(plain.size() + 64, '\0');
    stream.next_out = reinterpret_cast<unsigned char *>(deflated.data());
    stream.avail_out = static_cast<unsigned int>(deflated.size());
    EXPECT_EQ(mz_deflate(&stream, MZ_FINISH), MZ_STREAM_END);
    deflated.resize(static_cast<std::size_t>(stream.total_out));
    mz_deflateEnd(&stream);

    std::string out;
    out.push_back(static_cast<char>(0x1f));
    out.push_back(static_cast<char>(0x8b));
    out.push_back(8);
    out.push_back(0);
    out.append(4, '\0');
    out.push_back(0);
    out.push_back(static_cast<char>(255));
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

TEST(ClientTest, GetOk)
{
    LocalHttpServer server;
    server.SetResponse(200, "hello world", {{"X-Test", "1"}});

    auto r = mog::get(server.origin() + "/path");
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(r->status_code, 200);
    EXPECT_EQ(r->text(), "hello world");
    EXPECT_EQ(r->header("X-Test"), "1");
    EXPECT_EQ(r->backend, "embedded");
    EXPECT_GE(r->elapsed.count(), 0);
    EXPECT_EQ(server.Last().method, "GET");
    EXPECT_EQ(server.Last().target, "/path");
}

TEST(ClientTest, GzipBodyIsDecoded)
{
    LocalHttpServer server;
    const std::string plain = "hello compressed world";
    server.SetResponse(200, MakeGzipBody(plain),
                       {{"Content-Encoding", "gzip"}, {"Content-Type", "text/plain"}});

    auto r = mog::get(server.origin() + "/gz");
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(r->text(), plain);
    EXPECT_TRUE(r->header("Content-Encoding").empty());
    EXPECT_EQ(r->header("Content-Type"), "text/plain");

    bool saw_accept = false;
    for (const auto &h : server.Last().headers)
    {
        if (h.first == "Accept-Encoding" || h.first == "accept-encoding")
        {
            EXPECT_NE(h.second.find("gzip"), std::string::npos);
            saw_accept = true;
        }
    }
    EXPECT_TRUE(saw_accept);
}

TEST(ClientTest, DecompressCanBeDisabled)
{
    LocalHttpServer server;
    const std::string plain = "raw-bytes";
    const std::string gz = MakeGzipBody(plain);
    server.SetResponse(200, gz, {{"Content-Encoding", "gzip"}});

    mog::Options opt;
    opt.decompress = false;
    auto r = mog::get(server.origin() + "/gz", opt);
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(r->body, gz);
    EXPECT_EQ(r->header("Content-Encoding"), "gzip");
}

TEST(ClientTest, PostJson)
{
    LocalHttpServer server;
    server.SetResponse(201, "{\"ok\":true}");

    mog::Options opt;
    mog::WithJson(opt, R"({"a":1})");
    auto r = mog::post(server.origin() + "/api", opt);
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(r->status_code, 201);
    EXPECT_EQ(server.Last().method, "POST");
    EXPECT_EQ(server.Last().body, R"({"a":1})");
    bool saw_ct = false;
    for (const auto &h : server.Last().headers)
    {
        if (h.first == "Content-Type" || h.first == "content-type")
        {
            EXPECT_NE(h.second.find("application/json"), std::string::npos);
            saw_ct = true;
        }
    }
    EXPECT_TRUE(saw_ct);
}

TEST(ClientTest, PostForm)
{
    LocalHttpServer server;
    server.SetResponse(200, "ok");

    mog::Options opt;
    mog::WithForm(opt, {{"user", "a b"}, {"x", "1"}});
    auto r = mog::post(server.origin() + "/form", opt);
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_NE(server.Last().body.find("user=a+b"), std::string::npos);
    EXPECT_NE(server.Last().body.find("x=1"), std::string::npos);
}

TEST(ClientTest, BasicAuth)
{
    LocalHttpServer server;
    server.SetResponse(200, "secret");
    // "user:pass" base64
    server.RequireAuth("Basic " + mog::Base64Encode("user:pass"));

    mog::Options opt;
    mog::WithBasicAuth(opt, "user", "pass");
    auto r = mog::get(server.origin() + "/secure", opt);
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(r->status_code, 200);
    EXPECT_EQ(r->text(), "secret");
}

TEST(ClientTest, BearerAuth)
{
    LocalHttpServer server;
    server.SetResponse(200, "ok");
    server.RequireAuth("Bearer tok123");

    mog::Options opt;
    mog::WithBearerToken(opt, "tok123");
    auto r = mog::get(server.origin() + "/t", opt);
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(r->status_code, 200);
}

TEST(ClientTest, QueryParams)
{
    LocalHttpServer server;
    server.SetResponse(200, "ok");

    mog::Options opt;
    opt.params = {{"q", "hello world"}, {"n", "1"}};
    auto r = mog::get(server.origin() + "/search", opt);
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_NE(server.Last().target.find("q=hello"), std::string::npos);
    EXPECT_NE(server.Last().target.find("n=1"), std::string::npos);
}

TEST(ClientTest, CookiesRoundTrip)
{
    LocalHttpServer server;
    server.SetResponse(200, "ok", {{"Set-Cookie", "sid=abc; Path=/"}, {"Set-Cookie", "x=1"}});

    // Note: our simple server only sends one of each header name in map - we push vector of pairs
    // but when building response we can send two Set-Cookie lines.
    // SetResponse stores vector - good, two Set-Cookie entries work.

    auto r = mog::get(server.origin() + "/");
    ASSERT_TRUE(r) << r.error().to_string();
    // Server implementation: extra_headers with two Set-Cookie - CollectCookies should find both.
    // Our LocalHttpServer can emit multiple Set-Cookie from the vector.
    EXPECT_FALSE(r->cookies.empty());
}

TEST(ClientTest, SessionCookieJar)
{
    LocalHttpServer server;
    server.SetResponse(200, "ok", {{"Set-Cookie", "session=xyz"}});

    mog::Session s;
    auto r1 = s.get(server.origin() + "/login");
    ASSERT_TRUE(r1) << r1.error().to_string();
    EXPECT_EQ(s.cookies().count("session"), 1U);

    server.SetResponse(200, "ok");
    auto r2 = s.get(server.origin() + "/me");
    ASSERT_TRUE(r2) << r2.error().to_string();
    const auto last = server.Last();
    bool found = false;
    for (const auto &h : last.headers)
    {
        if (h.first == "Cookie" || h.first == "cookie")
        {
            EXPECT_NE(h.second.find("session=xyz"), std::string::npos);
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(ClientTest, RedirectFollow)
{
    LocalHttpServer server;
    server.SetPathRule("/start", "REDIRECT:/final");
    server.SetPathRule("/final", "landed");

    auto r = mog::get(server.origin() + "/start");
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(r->status_code, 200);
    EXPECT_EQ(r->text(), "landed");
    EXPECT_EQ(r->history_len, 1);
    EXPECT_EQ(r->url, server.origin() + "/final");
}

TEST(ClientTest, RaiseForStatus)
{
    LocalHttpServer server;
    server.SetResponse(404, "missing");
    auto r = mog::get(server.origin() + "/nope");
    ASSERT_TRUE(r);
    auto check = r->raise_for_status();
    EXPECT_FALSE(check);
    EXPECT_EQ(check.error().code(), mog::ErrorCode::HttpStatus);
}

TEST(ClientTest, MaxResponseBytes)
{
    LocalHttpServer server;
    server.SetResponse(200, std::string(1000, 'x'));
    mog::Options opt;
    opt.max_response_bytes = 100;
    auto r = mog::get(server.origin() + "/big", opt);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code(), mog::ErrorCode::ResponseTooLarge);
}

TEST(UtilTest, Base64AndForm)
{
    EXPECT_EQ(mog::Base64Encode("user:pass"), "dXNlcjpwYXNz");
    EXPECT_EQ(mog::EncodeForm({{"a", "b c"}}), "a=b+c");
    std::string name;
    std::string value;
    ASSERT_TRUE(mog::ParseSetCookie("sid=abc; Path=/; HttpOnly", name, value));
    EXPECT_EQ(name, "sid");
    EXPECT_EQ(value, "abc");
}
