/**
 * @file run_test.cpp
 * @brief Orchestration tests for mog::cli::Run (no network when prepare fails).
 */

#include "mog/cli.hpp"

#include <gtest/gtest.h>
#include <sstream>

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
