/**
 * @file backend_conformance_test.cpp
 * @brief Cross-backend HTTP contract: the behavior every backend must satisfy (#19).
 *
 * `RunHttpContract` exercises a single backend against the in-process server over
 * plain HTTP. The embedded backend runs it today; each native backend enables its
 * own case as it lands (skipped until its transport reports available). TLS-level
 * parity is out of scope here (no local TLS fixture).
 */

#include "http/detail/transport.hpp"
#include "mog/mog.hpp"
#include "test_support/local_http_server.hpp"

#include <gtest/gtest.h>
#include <string>

using mog::Backend;
using mog::test::LocalHttpServer;

namespace
{

mog::Options With(Backend backend)
{
    mog::Options opt;
    opt.backend = backend;
    return opt;
}

// The behavioral contract a conforming backend must meet over HTTP/1.1.
void RunHttpContract(Backend backend)
{
    // GET: status, body, and a response header round-trip.
    {
        LocalHttpServer server;
        server.SetResponse(200, "hello contract", {{"X-Contract", "1"}});
        auto r = mog::get(server.origin() + "/get", With(backend));
        ASSERT_TRUE(r) << r.error().to_string();
        EXPECT_EQ(r->status_code, 200);
        EXPECT_EQ(r->text(), "hello contract");
        EXPECT_EQ(r->header("X-Contract"), "1");
        EXPECT_EQ(server.Last().method, "GET");
    }

    // Request headers are sent to the server.
    {
        LocalHttpServer server;
        server.SetResponse(200, "ok");
        mog::Options opt = With(backend);
        opt.headers["X-Sent"] = "yes";
        auto r = mog::get(server.origin() + "/hdr", opt);
        ASSERT_TRUE(r) << r.error().to_string();
        const auto last = server.Last();
        auto it = last.headers.find("X-Sent");
        ASSERT_NE(it, last.headers.end());
        EXPECT_EQ(it->second, "yes");
    }

    // POST body is transmitted verbatim.
    {
        LocalHttpServer server;
        server.SetResponse(201, "created");
        mog::Options opt = With(backend);
        opt.body = "payload-body";
        opt.headers["Content-Type"] = "text/plain";
        auto r = mog::post(server.origin() + "/post", opt);
        ASSERT_TRUE(r) << r.error().to_string();
        EXPECT_EQ(r->status_code, 201);
        EXPECT_EQ(server.Last().method, "POST");
        EXPECT_EQ(server.Last().body, "payload-body");
    }

    // Query parameters reach the server target.
    {
        LocalHttpServer server;
        server.SetResponse(200, "ok");
        mog::Options opt = With(backend);
        opt.params["q"] = "search term";
        auto r = mog::get(server.origin() + "/q", opt);
        ASSERT_TRUE(r) << r.error().to_string();
        EXPECT_NE(server.Last().target.find("q=search"), std::string::npos);
    }

    // 404 is surfaced as a non-ok status (not an error).
    {
        LocalHttpServer server;
        server.SetResponse(404, "nope");
        auto r = mog::get(server.origin() + "/missing", With(backend));
        ASSERT_TRUE(r) << r.error().to_string();
        EXPECT_EQ(r->status_code, 404);
        EXPECT_FALSE(r->ok());
    }

    // Redirects are followed by default to the final 200.
    {
        LocalHttpServer server;
        server.SetPathRule("/start", "REDIRECT:/final");
        server.SetPathRule("/final", "landed");
        auto r = mog::get(server.origin() + "/start", With(backend));
        ASSERT_TRUE(r) << r.error().to_string();
        EXPECT_EQ(r->status_code, 200);
        EXPECT_EQ(r->text(), "landed");
    }

    // PUT and DELETE methods are conveyed.
    {
        LocalHttpServer server;
        server.SetResponse(200, "ok");
        auto put = mog::put(server.origin() + "/r", With(backend));
        ASSERT_TRUE(put) << put.error().to_string();
        EXPECT_EQ(server.Last().method, "PUT");
        auto del = mog::del(server.origin() + "/r", With(backend));
        ASSERT_TRUE(del) << del.error().to_string();
        EXPECT_EQ(server.Last().method, "DELETE");
    }
}

} // namespace

TEST(BackendConformance, Embedded)
{
    RunHttpContract(Backend::Embedded);
}

TEST(BackendConformance, NativeIfAvailable)
{
    if (!mog::detail::IsBackendAvailable(Backend::Native))
    {
        GTEST_SKIP() << "native backend not available on this platform/build";
    }
    RunHttpContract(Backend::Native);
}

TEST(BackendConformance, CurlIfAvailable)
{
    if (!mog::detail::IsBackendAvailable(Backend::Curl))
    {
        GTEST_SKIP() << "curl backend not available";
    }
    RunHttpContract(Backend::Curl);
}

TEST(BackendConformance, WinHttpIfAvailable)
{
    if (!mog::detail::IsBackendAvailable(Backend::WinHttp))
    {
        GTEST_SKIP() << "winhttp backend not available";
    }
    RunHttpContract(Backend::WinHttp);
}
