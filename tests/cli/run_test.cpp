/**
 * @file run_test.cpp
 * @brief Orchestration tests for mog::cli::Run (no network when prepare fails).
 */

#include "mog/cli.hpp"
#include "test_support/local_http_server.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>

TEST(CliRun, PrepareErrorInvalidBackend)
{
    mog::cli::Args args;
    args.url = "https://example.com/";
    args.backend = "not-a-backend";
    args.silent = true;

    std::ostringstream err;
    mog::cli::Streams streams;
    streams.err = &err;

    const int code = mog::cli::Run(args, streams);
    EXPECT_EQ(code, 2);
    EXPECT_FALSE(err.str().empty());
}

TEST(CliRun, PrepareErrorInvalidHeader)
{
    mog::cli::Args args;
    args.url = "https://example.com/";
    args.headers = {"BadHeader"};
    args.silent = true;

    std::ostringstream err;
    mog::cli::Streams streams;
    streams.err = &err;

    EXPECT_EQ(mog::cli::Run(args, streams), 2);
}

TEST(CliOutput, FormatHeaderBlock)
{
    mog::Response r;
    r.status_code = 200;
    r.reason = "OK";
    r.headers.push_back({"Content-Type", "text/plain"});
    const auto block = mog::cli::FormatHeaderBlock(r);
    EXPECT_NE(block.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(block.find("Content-Type: text/plain"), std::string::npos);
}

TEST(CliOutput, WriteResponseIncludeHeaders)
{
    mog::cli::Prepared p;
    p.include_headers = true;
    p.method = mog::Method::Get;
    mog::Response r;
    r.status_code = 200;
    r.reason = "OK";
    r.body = "hi";
    r.headers.push_back({"X-A", "1"});

    std::ostringstream out;
    auto ok = mog::cli::WriteResponseOutput(p, r, out);
    ASSERT_TRUE(ok);
    EXPECT_NE(out.str().find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(out.str().find("hi"), std::string::npos);
}

TEST(CliOutput, HeadSkipsBody)
{
    mog::cli::Prepared p;
    p.method = mog::Method::Head;
    mog::Response r;
    r.status_code = 200;
    r.body = "secret";
    std::ostringstream out;
    ASSERT_TRUE(mog::cli::WriteResponseOutput(p, r, out));
    EXPECT_EQ(out.str().find("secret"), std::string::npos);
}

TEST(CliRun, OutputFileStreamsBody)
{
    mog::test::LocalHttpServer server;
    std::string payload;
    payload.reserve(128 * 1024);
    for (std::size_t i = 0; i < 128 * 1024; ++i)
    {
        payload.push_back(static_cast<char>('a' + static_cast<int>(i % 26)));
    }
    server.SetResponse(200, payload);

    const auto path = std::filesystem::temp_directory_path() / "mog_cli_stream_out.bin";
    std::filesystem::remove(path);

    mog::cli::Args args;
    args.url = server.origin() + "/dl";
    args.output = path.string();
    args.write_out = "%{size_download}";
    args.silent = true;

    std::ostringstream err;
    mog::cli::Streams streams;
    std::ostringstream out;
    streams.out = &out;
    streams.err = &err;

    const int code = mog::cli::Run(args, streams);
    EXPECT_EQ(code, 0);

    std::string file_contents;
    {
        std::ifstream in(path, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        file_contents = ss.str();
    } // close the read handle before remove() — Windows locks open files
    EXPECT_EQ(file_contents, payload);
    EXPECT_TRUE(out.str().empty());                       // body went to the file, not stdout
    EXPECT_EQ(err.str(), std::to_string(payload.size())); // -w size_download

    std::filesystem::remove(path);
}
