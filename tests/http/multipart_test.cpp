/**
 * @file multipart_test.cpp
 * @brief Tests for multipart/form-data uploads (#7): body builder + end-to-end.
 */

#include "http/detail/multipart.hpp"
#include "mog/mog.hpp"
#include "test_support/local_http_server.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <vector>

using mog::FormPart;
using mog::detail::BuildMultipartBody;
using mog::detail::GuessContentType;
using mog::test::LocalHttpServer;

namespace
{

std::string HeaderValue(const mog::test::HttpExchange &ex, const std::string &name)
{
    for (const auto &h : ex.headers)
    {
        if (h.first.size() != name.size())
        {
            continue;
        }
        bool eq = true;
        for (std::size_t i = 0; i < name.size(); ++i)
        {
            if (std::tolower(static_cast<unsigned char>(h.first[i])) !=
                std::tolower(static_cast<unsigned char>(name[i])))
            {
                eq = false;
                break;
            }
        }
        if (eq)
        {
            return h.second;
        }
    }
    return {};
}

bool Contains(const std::string &haystack, const std::string &needle)
{
    return haystack.find(needle) != std::string::npos;
}

} // namespace

// --- Body builder (deterministic boundary) -----------------------------------

TEST(MultipartBody, FieldAndFileStructure)
{
    std::vector<FormPart> parts;
    parts.push_back(FormPart{"field", "val", std::nullopt, {}});
    parts.push_back(FormPart{"upload", "FILEDATA", std::optional<std::string>{"a.txt"}, {}});

    const std::string body = BuildMultipartBody(parts, "BOUNDARY");
    const std::string expected = "--BOUNDARY\r\n"
                                 "Content-Disposition: form-data; name=\"field\"\r\n"
                                 "\r\n"
                                 "val\r\n"
                                 "--BOUNDARY\r\n"
                                 "Content-Disposition: form-data; name=\"upload\"; "
                                 "filename=\"a.txt\"\r\n"
                                 "Content-Type: text/plain\r\n"
                                 "\r\n"
                                 "FILEDATA\r\n"
                                 "--BOUNDARY--\r\n";
    EXPECT_EQ(body, expected);
}

TEST(MultipartBody, ExplicitContentTypeWins)
{
    std::vector<FormPart> parts;
    parts.push_back(FormPart{"f", "bytes", std::optional<std::string>{"blob"}, "image/png"});
    const std::string body = BuildMultipartBody(parts, "B");
    EXPECT_TRUE(Contains(body, "Content-Type: image/png\r\n"));
}

TEST(MultipartBody, EscapesQuotesAndNewlinesInNames)
{
    std::vector<FormPart> parts;
    parts.push_back(FormPart{"a\"b", "v", std::optional<std::string>{"c\r\nd.txt"}, {}});
    const std::string body = BuildMultipartBody(parts, "B");
    EXPECT_TRUE(Contains(body, "name=\"a%22b\""));
    EXPECT_TRUE(Contains(body, "filename=\"c%0D%0Ad.txt\""));
}

TEST(MultipartBody, GuessContentTypeByExtension)
{
    EXPECT_EQ(GuessContentType("photo.png"), "image/png");
    EXPECT_EQ(GuessContentType("data.JSON"), "application/json"); // case-insensitive
    EXPECT_EQ(GuessContentType("notes.txt"), "text/plain");
    EXPECT_EQ(GuessContentType("archive.tar"), "application/octet-stream");
    EXPECT_EQ(GuessContentType("noext"), "application/octet-stream");
}

// --- End-to-end via the loopback server --------------------------------------

TEST(Multipart, PostSetsContentTypeAndBoundary)
{
    LocalHttpServer server;
    server.SetResponse(200, "ok");

    mog::Options opt;
    mog::AddFormField(opt, "user", "alice");
    // Embedded NUL built explicitly (avoids the \xNN hex-escape swallowing digits).
    const std::string binary = std::string("BINARY") + '\0' + "DATA";
    mog::AddFormFile(opt, "file", "data.bin", binary, "application/octet-stream");

    auto r = mog::post(server.origin() + "/upload", opt);
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(r->status_code, 200);

    const auto last = server.Last();
    EXPECT_EQ(last.method, "POST");
    const std::string ct = HeaderValue(last, "Content-Type");
    EXPECT_TRUE(Contains(ct, "multipart/form-data; boundary=----mogFormBoundary")) << ct;

    // The declared boundary must actually delimit the captured body.
    const auto bpos = ct.find("boundary=");
    ASSERT_NE(bpos, std::string::npos);
    const std::string boundary = ct.substr(bpos + 9);
    EXPECT_TRUE(Contains(last.body, "--" + boundary + "\r\n"));
    EXPECT_TRUE(Contains(last.body, "--" + boundary + "--\r\n"));

    EXPECT_TRUE(Contains(last.body, "Content-Disposition: form-data; name=\"user\""));
    EXPECT_TRUE(Contains(last.body, "alice"));
    EXPECT_TRUE(Contains(last.body, "name=\"file\"; filename=\"data.bin\""));
    EXPECT_TRUE(Contains(last.body, "Content-Type: application/octet-stream"));
    EXPECT_EQ(last.body.find("BINARY"), last.body.rfind("BINARY")); // binary bytes preserved once
}

TEST(Multipart, GetWithPartsBecomesPost)
{
    LocalHttpServer server;
    server.SetResponse(200, "ok");

    mog::Options opt;
    mog::AddFormField(opt, "k", "v");
    auto r = mog::request(mog::Method::Get, server.origin() + "/x", opt);
    ASSERT_TRUE(r) << r.error().to_string();
    // Free functions don't auto-promote the method; the caller chose GET, and the
    // multipart body is still sent. (CLI promotes GET->POST; see run_test.)
    EXPECT_TRUE(Contains(HeaderValue(server.Last(), "Content-Type"), "multipart/form-data"));
}

TEST(Multipart, AddFormFileFromPathReadsFile)
{
    const auto path = std::filesystem::temp_directory_path() / "mog_multipart_src.txt";
    {
        std::ofstream out(path, std::ios::binary);
        out << "hello file contents";
    }

    mog::Options opt;
    auto added = mog::AddFormFileFromPath(opt, "doc", path.string());
    ASSERT_TRUE(added) << added.error().to_string();
    ASSERT_EQ(opt.multipart.size(), 1U);
    EXPECT_EQ(opt.multipart[0].name, "doc");
    EXPECT_EQ(opt.multipart[0].value, "hello file contents");
    ASSERT_TRUE(opt.multipart[0].filename.has_value());
    EXPECT_EQ(*opt.multipart[0].filename, "mog_multipart_src.txt");

    std::filesystem::remove(path);
}

TEST(Multipart, AddFormFileFromPathReportsMissingFile)
{
    mog::Options opt;
    auto added = mog::AddFormFileFromPath(opt, "doc", "/no/such/file/here.txt");
    ASSERT_FALSE(added);
    EXPECT_EQ(added.error().code(), mog::ErrorCode::FileError);
}
