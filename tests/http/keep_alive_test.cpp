/**
 * @file keep_alive_test.cpp
 * @brief Session HTTP/1.1 keep-alive / connection reuse tests.
 */

#include "mog/mog.hpp"
#include "test_support/local_http_server.hpp"

#include <gtest/gtest.h>

using mog::test::LocalHttpServer;

TEST(KeepAlive, SessionReusesConnection)
{
    LocalHttpServer server;
    server.SetResponse(200, "one", {}, /*chunked=*/false, /*keep_alive=*/true);

    mog::Session session;
    auto r1 = session.get(server.origin() + "/a");
    ASSERT_TRUE(r1) << r1.error().to_string();
    EXPECT_EQ(r1->text(), "one");

    server.SetResponse(200, "two", {}, false, true);
    auto r2 = session.get(server.origin() + "/b");
    ASSERT_TRUE(r2) << r2.error().to_string();
    EXPECT_EQ(r2->text(), "two");

    // One TCP accept for both requests when keep-alive works.
    EXPECT_EQ(server.connection_count(), 1U);
    EXPECT_EQ(server.History().size(), 2U);
    EXPECT_EQ(server.History()[0].method, "GET");
    EXPECT_EQ(server.History()[1].method, "GET");
}

TEST(KeepAlive, FreeFunctionsDoNotPoolByDefault)
{
    LocalHttpServer server;
    server.SetResponse(200, "x", {}, false, true);

    auto r1 = mog::get(server.origin() + "/1");
    ASSERT_TRUE(r1) << r1.error().to_string();
    auto r2 = mog::get(server.origin() + "/2");
    ASSERT_TRUE(r2) << r2.error().to_string();

    // No pool → new connection each time even if server offers keep-alive.
    EXPECT_EQ(server.connection_count(), 2U);
}

TEST(KeepAlive, SessionKeepAliveDisabledUsesNewConnections)
{
    LocalHttpServer server;
    server.SetResponse(200, "ok", {}, false, true);

    mog::Session session;
    mog::Options opt;
    opt.keep_alive = false;
    auto r1 = session.get(server.origin() + "/a", opt);
    ASSERT_TRUE(r1) << r1.error().to_string();
    auto r2 = session.get(server.origin() + "/b", opt);
    ASSERT_TRUE(r2) << r2.error().to_string();
    EXPECT_EQ(server.connection_count(), 2U);
}

TEST(KeepAlive, ServerConnectionClosePreventsReuse)
{
    LocalHttpServer server;
    // Server closes after each response.
    server.SetResponse(200, "a", {}, false, /*keep_alive=*/false);

    mog::Session session;
    auto r1 = session.get(server.origin() + "/1");
    ASSERT_TRUE(r1) << r1.error().to_string();
    auto r2 = session.get(server.origin() + "/2");
    ASSERT_TRUE(r2) << r2.error().to_string();
    EXPECT_EQ(server.connection_count(), 2U);
}

TEST(KeepAlive, SessionSendsKeepAliveHeader)
{
    LocalHttpServer server;
    server.SetResponse(200, "ok", {}, false, true);

    mog::Session session;
    auto r = session.get(server.origin() + "/h");
    ASSERT_TRUE(r) << r.error().to_string();

    bool saw = false;
    for (const auto &h : server.Last().headers)
    {
        if (h.first == "Connection" || h.first == "connection")
        {
            EXPECT_NE(h.second.find("keep-alive"), std::string::npos);
            saw = true;
        }
    }
    EXPECT_TRUE(saw);
}

TEST(KeepAlive, RedirectsStillFollowedByDefaultOnSession)
{
    LocalHttpServer server;
    server.SetPathRule("/start", "REDIRECT:/end");
    server.SetPathRule("/end", "done");
    // Keep-alive on path rules defaults false (close) — still must follow redirect.
    auto r = mog::Session{}.get(server.origin() + "/start");
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(r->text(), "done");
    EXPECT_EQ(r->history_len, 1);
}

TEST(RedirectDefaults, FreeFunctionsFollowRedirects)
{
    LocalHttpServer server;
    server.SetPathRule("/a", "REDIRECT:/b");
    server.SetPathRule("/b", "landed");
    auto r = mog::get(server.origin() + "/a");
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(r->text(), "landed");
}

TEST(RedirectDefaults, CanDisableWithAllowRedirectsFalse)
{
    LocalHttpServer server;
    mog::test::HttpResponseSpec redir;
    redir.status = 302;
    redir.location = "/elsewhere";
    server.SetPathResponse("/here", redir);

    mog::Options opt;
    opt.allow_redirects = false;
    auto r = mog::get(server.origin() + "/here", opt);
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(r->status_code, 302);
    EXPECT_EQ(r->header("Location"), "/elsewhere");
}
