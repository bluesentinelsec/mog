// Deterministic tests for the C API (<mog/mog_c.h>).
//
// These drive the C entry points against a local loopback server using the
// embedded backend, so behavior is stable across platforms. The pure-C ABI is
// exercised separately by compile_check.c and ctypes_smoke.py.

#include <mog/mog_c.h>

#include "test_support/local_http_server.hpp"

#include <gtest/gtest.h>

#include <cctype>
#include <cstring>
#include <map>
#include <string>

using mog::test::LocalHttpServer;

namespace
{

// Build a GET request pinned to the embedded backend for deterministic results.
mog_request *NewEmbeddedGet(const std::string &url)
{
    mog_request *req = mog_request_new("GET", url.c_str());
    mog_request_set_backend(req, "embedded");
    return req;
}

// Case-insensitive header lookup on the server's captured request. Returns
// nullptr when absent (header casing on the wire is backend-dependent).
const std::string *FindHeader(const std::map<std::string, std::string> &headers, const char *name)
{
    for (const auto &kv : headers)
    {
        if (kv.first.size() != std::strlen(name))
        {
            continue;
        }
        bool equal = true;
        for (std::size_t i = 0; i < kv.first.size(); ++i)
        {
            if (std::tolower(static_cast<unsigned char>(kv.first[i])) !=
                std::tolower(static_cast<unsigned char>(name[i])))
            {
                equal = false;
                break;
            }
        }
        if (equal)
        {
            return &kv.second;
        }
    }
    return nullptr;
}

} // namespace

TEST(MogCApi, VersionIsNonEmpty)
{
    const char *v = mog_version();
    ASSERT_NE(v, nullptr);
    EXPECT_GT(std::strlen(v), 0u);
}

TEST(MogCApi, ErrorCodeNameIsStable)
{
    EXPECT_STREQ(mog_error_code_name(MOG_OK), "ok");
    EXPECT_STREQ(mog_error_code_name(MOG_ERR_TIMEOUT), "timeout");
}

TEST(MogCApi, GetReturnsStatusBodyAndHeaders)
{
    LocalHttpServer server;
    server.SetResponse(200, "hello world", {{"X-Test", "42"}});

    mog_request *req = NewEmbeddedGet(server.origin() + "/path");
    ASSERT_NE(req, nullptr);
    mog_response *resp = mog_perform(req);
    ASSERT_NE(resp, nullptr);

    EXPECT_EQ(mog_response_ok(resp), 1);
    EXPECT_EQ(mog_response_error_code(resp), MOG_OK);
    EXPECT_STREQ(mog_response_error_message(resp), "");
    EXPECT_EQ(mog_response_status(resp), 200);

    size_t len = 0;
    const char *body = mog_response_body(resp, &len);
    EXPECT_EQ(len, std::strlen("hello world"));
    EXPECT_EQ(std::string(body, len), "hello world");
    EXPECT_EQ(mog_response_body_size(resp), len);

    // Case-insensitive header lookup.
    EXPECT_STREQ(mog_response_header(resp, "x-test"), "42");
    EXPECT_GT(mog_response_header_count(resp), 0u);
    EXPECT_STREQ(mog_response_backend(resp), "embedded");

    mog_response_free(resp);
    mog_request_free(req);
}

TEST(MogCApi, SendsHeadersQueryAndBody)
{
    LocalHttpServer server;
    server.SetResponse(200, "ok");

    mog_request *req = mog_request_new("POST", (server.origin() + "/submit").c_str());
    ASSERT_NE(req, nullptr);
    mog_request_set_backend(req, "embedded");
    mog_request_set_header(req, "X-Custom", "value1");
    mog_request_set_query_param(req, "q", "search");
    const std::string payload = "field=1";
    mog_request_set_body(req, payload.data(), payload.size());

    mog_response *resp = mog_perform(req);
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(mog_response_ok(resp), 1);
    EXPECT_EQ(mog_response_status(resp), 200);

    const auto last = server.Last();
    EXPECT_EQ(last.method, "POST");
    EXPECT_NE(last.target.find("q=search"), std::string::npos);
    EXPECT_NE(FindHeader(last.headers, "X-Custom"), nullptr);
    EXPECT_EQ(last.body, payload);

    mog_response_free(resp);
    mog_request_free(req);
}

TEST(MogCApi, JsonSetsContentType)
{
    LocalHttpServer server;
    server.SetResponse(200, "ok");

    mog_request *req = mog_request_new("POST", (server.origin() + "/api").c_str());
    ASSERT_NE(req, nullptr);
    mog_request_set_backend(req, "embedded");
    mog_request_set_json(req, "{\"name\":\"mog\"}");

    mog_response *resp = mog_perform(req);
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(mog_response_ok(resp), 1);

    const auto last = server.Last();
    EXPECT_EQ(last.body, "{\"name\":\"mog\"}");
    const std::string *content_type = FindHeader(last.headers, "Content-Type");
    ASSERT_NE(content_type, nullptr);
    EXPECT_NE(content_type->find("application/json"), std::string::npos);

    mog_response_free(resp);
    mog_request_free(req);
}

TEST(MogCApi, BasicAuthHeaderIsSent)
{
    LocalHttpServer server;
    server.SetResponse(200, "ok");

    mog_request *req = NewEmbeddedGet(server.origin() + "/secure");
    ASSERT_NE(req, nullptr);
    mog_request_set_basic_auth(req, "user", "pass");

    mog_response *resp = mog_perform(req);
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(mog_response_ok(resp), 1);

    const auto last = server.Last();
    const std::string *authorization = FindHeader(last.headers, "Authorization");
    ASSERT_NE(authorization, nullptr);
    EXPECT_EQ(authorization->rfind("Basic ", 0), 0u);

    mog_response_free(resp);
    mog_request_free(req);
}

TEST(MogCApi, TransportErrorSurfacesWithoutCrashing)
{
    // Nothing is listening on this port; connect fails and ok() is 0.
    mog_request *req = mog_request_new("GET", "http://127.0.0.1:9/");
    ASSERT_NE(req, nullptr);
    mog_request_set_backend(req, "embedded");
    mog_request_set_connect_timeout_ms(req, 1500);

    mog_response *resp = mog_perform(req);
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(mog_response_ok(resp), 0);
    EXPECT_NE(mog_response_error_code(resp), MOG_OK);
    EXPECT_GT(std::strlen(mog_response_error_message(resp)), 0u);
    // Accessors on a failed response return safe defaults, not crashes.
    EXPECT_EQ(mog_response_status(resp), 0);
    EXPECT_STREQ(mog_response_body(resp, nullptr), "");

    mog_response_free(resp);
    mog_request_free(req);
}

TEST(MogCApi, InvalidArgumentsReturnNull)
{
    EXPECT_EQ(mog_request_new(nullptr, "http://x/"), nullptr);
    EXPECT_EQ(mog_request_new("GET", nullptr), nullptr);
    EXPECT_EQ(mog_request_new("GET", ""), nullptr);
    EXPECT_EQ(mog_request_new("NOTAMETHOD", "http://x/"), nullptr);
    EXPECT_EQ(mog_get(nullptr), nullptr);

    // NULL handles are tolerated by every accessor and by free().
    EXPECT_EQ(mog_response_ok(nullptr), 0);
    EXPECT_STREQ(mog_response_body(nullptr, nullptr), "");
    EXPECT_EQ(mog_response_header_count(nullptr), 0u);
    mog_response_free(nullptr);
    mog_request_free(nullptr);
}
