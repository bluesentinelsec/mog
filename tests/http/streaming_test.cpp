/**
 * @file streaming_test.cpp
 * @brief Tests for streaming response bodies to a writer / file (issue #5).
 */

#include "mog/mog.hpp"
#include "test_support/local_http_server.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <string_view>

using mog::test::HttpResponseSpec;
using mog::test::LocalHttpServer;

namespace
{

std::string MakePayload(std::size_t n)
{
    std::string s;
    s.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        s.push_back(static_cast<char>('A' + static_cast<int>(i % 26)));
    }
    return s;
}

// A response writer that appends every delivered chunk into `sink`.
mog::BodyWriter CollectInto(std::string &sink, int *calls = nullptr)
{
    return [&sink, calls](std::string_view data) -> mog::Result<void> {
        if (calls != nullptr)
        {
            ++(*calls);
        }
        sink.append(data.data(), data.size());
        return mog::Result<void>::Ok();
    };
}

std::string ReadWholeFile(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

TEST(Streaming, WriterReceivesContentLengthBody)
{
    LocalHttpServer server;
    const std::string payload = MakePayload(256 * 1024); // multi-read, > 16 KiB block
    server.SetResponse(200, payload, {{"Content-Type", "application/octet-stream"}});

    std::string got;
    mog::Options opts;
    opts.response_writer = CollectInto(got);

    auto r = mog::get(server.origin() + "/big", opts);
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(r->status_code, 200);
    EXPECT_EQ(got, payload);
    EXPECT_TRUE(r->body.empty()); // body is not buffered when streaming
    EXPECT_EQ(r->downloaded_bytes, payload.size());
}

TEST(Streaming, WriterReceivesChunkedBody)
{
    LocalHttpServer server;
    const std::string payload = MakePayload(4096); // server emits 8-byte chunks
    HttpResponseSpec spec;
    spec.status = 200;
    spec.body = payload;
    spec.chunked = true;
    server.SetPathResponse("/chunked", spec);

    std::string got;
    int calls = 0;
    mog::Options opts;
    opts.response_writer = CollectInto(got, &calls);

    auto r = mog::get(server.origin() + "/chunked", opts);
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(got, payload);
    EXPECT_EQ(r->downloaded_bytes, payload.size());
    EXPECT_GT(calls, 1); // delivered incrementally, not in one shot
}

TEST(Streaming, FileWriterWritesBodyToDisk)
{
    LocalHttpServer server;
    const std::string payload = MakePayload(200 * 1024);
    server.SetResponse(200, payload);

    const auto path = std::filesystem::temp_directory_path() / "mog_stream_filewriter.bin";
    std::filesystem::remove(path);

    auto writer = mog::FileWriter(path.string());
    ASSERT_TRUE(writer) << writer.error().to_string();

    mog::Options opts;
    opts.response_writer = std::move(*writer);

    auto r = mog::get(server.origin() + "/download", opts);
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(r->downloaded_bytes, payload.size());
    EXPECT_TRUE(r->body.empty());

    // Drop the Options (and thus the writer/file) so the stream flushes and closes.
    opts.response_writer = nullptr;
    EXPECT_EQ(ReadWholeFile(path), payload);
    std::filesystem::remove(path);
}

TEST(Streaming, FileWriterReportsOpenFailure)
{
    auto writer = mog::FileWriter("/definitely/not/a/real/dir/out.bin");
    ASSERT_FALSE(writer);
    EXPECT_EQ(writer.error().code(), mog::ErrorCode::FileError);
}

TEST(Streaming, RespectsMaxResponseBytesContentLength)
{
    LocalHttpServer server;
    server.SetResponse(200, MakePayload(64 * 1024));

    std::string got;
    mog::Options opts;
    opts.response_writer = CollectInto(got);
    opts.max_response_bytes = 8 * 1024; // smaller than the body

    auto r = mog::get(server.origin() + "/capped", opts);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code(), mog::ErrorCode::ResponseTooLarge);
}

TEST(Streaming, RespectsMaxResponseBytesChunked)
{
    LocalHttpServer server;
    HttpResponseSpec spec;
    spec.status = 200;
    spec.body = MakePayload(64 * 1024);
    spec.chunked = true;
    server.SetPathResponse("/cap", spec);

    std::string got;
    mog::Options opts;
    opts.response_writer = CollectInto(got);
    opts.max_response_bytes = 4 * 1024;

    auto r = mog::get(server.origin() + "/cap", opts);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code(), mog::ErrorCode::ResponseTooLarge);
}

TEST(Streaming, DoesNotAdvertiseAcceptEncoding)
{
    LocalHttpServer server;
    server.SetResponse(200, "plain");

    std::string got;
    mog::Options opts;
    opts.response_writer = CollectInto(got);
    // decompress defaults to true, but a writer must suppress Accept-Encoding.

    auto r = mog::get(server.origin() + "/ae", opts);
    ASSERT_TRUE(r) << r.error().to_string();

    for (const auto &h : server.Last().headers)
    {
        if (h.first == "Accept-Encoding" || h.first == "accept-encoding")
        {
            ADD_FAILURE() << "streaming request advertised Accept-Encoding: " << h.second;
        }
    }
}

TEST(Streaming, OnlyFinalBodyStreamsAcrossRedirect)
{
    LocalHttpServer server;
    HttpResponseSpec redirect;
    redirect.status = 302;
    redirect.location = "/final";
    redirect.body = "REDIRECT-BODY-SHOULD-NOT-STREAM";
    server.SetPathResponse("/start", redirect);

    HttpResponseSpec final_spec;
    final_spec.status = 200;
    final_spec.body = "final-payload";
    server.SetPathResponse("/final", final_spec);

    std::string got;
    mog::Options opts;
    opts.response_writer = CollectInto(got);

    auto r = mog::get(server.origin() + "/start", opts);
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(r->status_code, 200);
    EXPECT_EQ(got, "final-payload");
    EXPECT_EQ(r->history_len, 1);
    EXPECT_EQ(r->downloaded_bytes, std::string("final-payload").size());
}

TEST(Streaming, WriterErrorAbortsRequest)
{
    LocalHttpServer server;
    server.SetResponse(200, MakePayload(32 * 1024));

    mog::Options opts;
    opts.response_writer = [](std::string_view) -> mog::Result<void> {
        return mog::Result<void>::Err(mog::Error{mog::ErrorCode::IoError, "sink boom"});
    };

    auto r = mog::get(server.origin() + "/fail", opts);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code(), mog::ErrorCode::IoError);
}

TEST(Streaming, SessionStreamsBody)
{
    LocalHttpServer server;
    const std::string payload = MakePayload(50 * 1024);
    server.SetResponse(200, payload);

    mog::Session session;
    std::string got;
    mog::Options opts;
    opts.response_writer = CollectInto(got);

    auto r = session.get(server.origin() + "/s", opts);
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(got, payload);
    EXPECT_EQ(r->downloaded_bytes, payload.size());
}

TEST(Streaming, BufferedResponseSetsDownloadedBytes)
{
    LocalHttpServer server;
    server.SetResponse(200, "hello world");

    auto r = mog::get(server.origin() + "/b");
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(r->body, "hello world");
    EXPECT_EQ(r->downloaded_bytes, r->body.size());
}
