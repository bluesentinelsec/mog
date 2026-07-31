/**
 * @file backend_conformance_test.cpp
 * @brief Cross-backend HTTP contract: the behavior every backend must satisfy (#19).
 *
 * `RunHttpContract` exercises a single backend against the in-process server over
 * plain HTTP. The embedded backend runs it today; each native backend enables its
 * own case as it lands (skipped until its transport reports available). TLS-level
 * parity is out of scope here (no local TLS fixture).
 */

#include "http/detail/env.hpp"
#include "http/detail/transport.hpp"
#include "mog/mog.hpp"
#include "test_support/local_http_server.hpp"

#include <gtest/gtest.h>
#include <string>
#include <vector>

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

    // Streaming: body delivered incrementally to the writer; Response::body empty.
    {
        LocalHttpServer server;
        const std::string payload(200 * 1024, 'x'); // large enough to arrive in pieces
        server.SetResponse(200, payload);
        std::string got;
        mog::Options opt = With(backend);
        opt.response_writer = [&got](std::string_view d) -> mog::Result<void> {
            got.append(d.data(), d.size());
            return mog::Result<void>::Ok();
        };
        auto r = mog::get(server.origin() + "/stream", opt);
        ASSERT_TRUE(r) << r.error().to_string();
        EXPECT_EQ(got, payload);
        EXPECT_TRUE(r->body.empty());
        EXPECT_EQ(r->downloaded_bytes, payload.size());
    }

    // max_response_bytes is enforced.
    {
        LocalHttpServer server;
        server.SetResponse(200, std::string(64 * 1024, 'y'));
        mog::Options opt = With(backend);
        opt.max_response_bytes = 8 * 1024;
        auto r = mog::get(server.origin() + "/cap", opt);
        ASSERT_FALSE(r);
        EXPECT_EQ(r.error().code(), mog::ErrorCode::ResponseTooLarge);
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

// CI hardening: MOG_CI_ENFORCE_BACKENDS (comma-separated names) turns the
// "skip when unavailable" cases above into hard failures, so a native backend
// silently regressing to unavailable on its own OS fails CI instead of quietly
// skipping. Unset locally (and in the sanitizer job), so this skips there.
TEST(BackendConformance, EnforcedBackendsAvailableInCi)
{
    const auto spec = mog::detail::GetEnv("MOG_CI_ENFORCE_BACKENDS");
    if (!spec.has_value() || spec->empty())
    {
        GTEST_SKIP() << "MOG_CI_ENFORCE_BACKENDS not set";
    }

    std::vector<std::string> names;
    std::string current;
    for (const char c : *spec)
    {
        if (c == ',')
        {
            if (!current.empty())
            {
                names.push_back(current);
            }
            current.clear();
        }
        else if (c != ' ')
        {
            current.push_back(c);
        }
    }
    if (!current.empty())
    {
        names.push_back(current);
    }
    ASSERT_FALSE(names.empty());

    for (const auto &name : names)
    {
        const auto backend = mog::ParseBackend(name);
        ASSERT_TRUE(backend.has_value()) << "unknown backend '" << name << "'";
        EXPECT_TRUE(mog::detail::IsBackendAvailable(*backend))
            << "backend '" << name << "' is expected to be available on this CI OS but is not";
        if (mog::detail::IsBackendAvailable(*backend))
        {
            RunHttpContract(*backend); // and it must satisfy the contract
        }
    }
}
