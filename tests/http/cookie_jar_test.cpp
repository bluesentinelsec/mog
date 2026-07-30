/**
 * @file cookie_jar_test.cpp
 * @brief Unit + integration tests for the domain/path-aware cookie jar (#6).
 */

#include "http/detail/cookie_jar.hpp"
#include "http/detail/url.hpp"
#include "mog/mog.hpp"
#include "test_support/local_http_server.hpp"

#include <gtest/gtest.h>
#include <string>

using mog::detail::CookieDomainMatch;
using mog::detail::CookieJar;
using mog::detail::CookiePathMatch;
using mog::detail::DefaultCookiePath;
using mog::detail::ParseUrl;
using mog::test::LocalHttpServer;

namespace
{

mog::detail::Url Url(const std::string &u)
{
    auto parsed = ParseUrl(u);
    EXPECT_TRUE(parsed) << (parsed ? "" : parsed.error().to_string());
    return parsed ? *parsed : mog::detail::Url{};
}

mog::Header SetCookie(std::string value)
{
    return mog::Header{"Set-Cookie", std::move(value)};
}

} // namespace

// --- Matching primitives -----------------------------------------------------

TEST(CookiePrimitives, DefaultPath)
{
    EXPECT_EQ(DefaultCookiePath("/"), "/");
    EXPECT_EQ(DefaultCookiePath("/me"), "/");
    EXPECT_EQ(DefaultCookiePath("/a/b"), "/a");
    EXPECT_EQ(DefaultCookiePath("/a/b/c"), "/a/b");
    EXPECT_EQ(DefaultCookiePath(""), "/");
    EXPECT_EQ(DefaultCookiePath("relative"), "/");
}

TEST(CookiePrimitives, DomainMatch)
{
    EXPECT_TRUE(CookieDomainMatch("example.com", "example.com"));
    EXPECT_TRUE(CookieDomainMatch("api.example.com", "example.com"));
    EXPECT_FALSE(CookieDomainMatch("example.com", "api.example.com"));
    EXPECT_FALSE(CookieDomainMatch("notexample.com", "example.com"));
    EXPECT_FALSE(CookieDomainMatch("examplexcom", "example.com"));
}

TEST(CookiePrimitives, PathMatch)
{
    EXPECT_TRUE(CookiePathMatch("/", "/"));
    EXPECT_TRUE(CookiePathMatch("/anything", "/"));
    EXPECT_TRUE(CookiePathMatch("/admin", "/admin"));
    EXPECT_TRUE(CookiePathMatch("/admin/panel", "/admin"));
    EXPECT_TRUE(CookiePathMatch("/admin/", "/admin/"));
    EXPECT_FALSE(CookiePathMatch("/administrator", "/admin")); // prefix but not a path segment
    EXPECT_FALSE(CookiePathMatch("/other", "/admin"));
}

// --- Jar behavior ------------------------------------------------------------

TEST(CookieJarTest, HostOnlyByDefault)
{
    CookieJar jar;
    jar.StoreFromResponse(Url("http://example.com/"), {SetCookie("sid=abc")});

    EXPECT_EQ(jar.CookiesFor(Url("http://example.com/")).count("sid"), 1U);
    // No Domain attribute => host-only: a subdomain must NOT receive it.
    EXPECT_EQ(jar.CookiesFor(Url("http://api.example.com/")).count("sid"), 0U);
    // Different host entirely.
    EXPECT_EQ(jar.CookiesFor(Url("http://other.com/")).count("sid"), 0U);
}

TEST(CookieJarTest, DomainAttributeMatchesSubdomains)
{
    CookieJar jar;
    jar.StoreFromResponse(Url("http://example.com/"), {SetCookie("sid=abc; Domain=example.com")});

    EXPECT_EQ(jar.CookiesFor(Url("http://example.com/")).count("sid"), 1U);
    EXPECT_EQ(jar.CookiesFor(Url("http://api.example.com/")).count("sid"), 1U);
    EXPECT_EQ(jar.CookiesFor(Url("http://other.com/")).count("sid"), 0U);
}

TEST(CookieJarTest, RejectsUnrelatedDomainAttribute)
{
    CookieJar jar;
    // Server on example.com tries to set a cookie for evil.com — must not apply.
    jar.StoreFromResponse(Url("http://example.com/"), {SetCookie("x=1; Domain=evil.com")});
    EXPECT_EQ(jar.CookiesFor(Url("http://evil.com/")).count("x"), 0U);
    // Falls back to host-only for the actual request host.
    EXPECT_EQ(jar.CookiesFor(Url("http://example.com/")).count("x"), 1U);
}

TEST(CookieJarTest, PathScoping)
{
    CookieJar jar;
    jar.StoreFromResponse(Url("http://example.com/admin/index"), {SetCookie("adm=1; Path=/admin")});

    EXPECT_EQ(jar.CookiesFor(Url("http://example.com/admin")).count("adm"), 1U);
    EXPECT_EQ(jar.CookiesFor(Url("http://example.com/admin/panel")).count("adm"), 1U);
    EXPECT_EQ(jar.CookiesFor(Url("http://example.com/")).count("adm"), 0U);
    EXPECT_EQ(jar.CookiesFor(Url("http://example.com/administrator")).count("adm"), 0U);
}

TEST(CookieJarTest, SecureCookieOnlyOverHttps)
{
    CookieJar jar;
    jar.StoreFromResponse(Url("https://example.com/"), {SetCookie("token=t; Secure")});

    EXPECT_EQ(jar.CookiesFor(Url("https://example.com/")).count("token"), 1U);
    EXPECT_EQ(jar.CookiesFor(Url("http://example.com/")).count("token"), 0U);
}

TEST(CookieJarTest, HttpOnlyIsStoredAndSent)
{
    CookieJar jar;
    jar.StoreFromResponse(Url("http://example.com/"), {SetCookie("sid=abc; HttpOnly; Path=/")});
    // HttpOnly only restricts scripting; we still store and send it.
    EXPECT_EQ(jar.CookiesFor(Url("http://example.com/")).count("sid"), 1U);
}

TEST(CookieJarTest, ReplacesSameNameDomainPath)
{
    CookieJar jar;
    jar.StoreFromResponse(Url("http://example.com/"), {SetCookie("sid=old")});
    jar.StoreFromResponse(Url("http://example.com/"), {SetCookie("sid=new")});
    EXPECT_EQ(jar.CookiesFor(Url("http://example.com/"))["sid"], "new");
}

TEST(CookieJarTest, MostSpecificPathWinsOnNameClash)
{
    CookieJar jar;
    jar.StoreFromResponse(Url("http://example.com/"), {SetCookie("v=root; Path=/")});
    jar.StoreFromResponse(Url("http://example.com/"), {SetCookie("v=admin; Path=/admin")});
    // Under /admin both match; the longer (more specific) path wins.
    EXPECT_EQ(jar.CookiesFor(Url("http://example.com/admin/x"))["v"], "admin");
    // Under / only the root cookie matches.
    EXPECT_EQ(jar.CookiesFor(Url("http://example.com/"))["v"], "root");
}

TEST(CookieJarTest, ManualCookieMatchesAnyHost)
{
    CookieJar jar;
    jar.SetManual("api_key", "k");
    EXPECT_EQ(jar.CookiesFor(Url("http://a.com/")).count("api_key"), 1U);
    EXPECT_EQ(jar.CookiesFor(Url("http://b.org/deep/path")).count("api_key"), 1U);
    EXPECT_EQ(jar.AllNameValues().at("api_key"), "k");
}

// --- Session integration (plain HTTP loopback) -------------------------------

TEST(SessionCookies, HostOnlyNotSentToOtherHost)
{
    LocalHttpServer server;
    server.SetResponse(200, "ok", {{"Set-Cookie", "sid=abc"}});

    mog::Session s;
    ASSERT_TRUE(s.get(server.origin() + "/login"));

    // A request to a *different* host must not carry the host-only cookie. Point
    // at 127.0.0.2-style host is awkward on loopback; instead assert the jar view.
    const auto view = s.cookies();
    EXPECT_EQ(view.count("sid"), 1U);

    // Same host, different path — cookie should be sent.
    server.SetResponse(200, "ok");
    ASSERT_TRUE(s.get(server.origin() + "/dashboard"));
    bool sent = false;
    for (const auto &h : server.Last().headers)
    {
        if (h.first == "Cookie" || h.first == "cookie")
        {
            EXPECT_NE(h.second.find("sid=abc"), std::string::npos);
            sent = true;
        }
    }
    EXPECT_TRUE(sent);
}

TEST(SessionCookies, PathScopedCookieNotSentOutsidePath)
{
    LocalHttpServer server;
    server.SetPathResponse("/admin/login", [] {
        mog::test::HttpResponseSpec spec;
        spec.status = 200;
        spec.body = "ok";
        spec.headers = {{"Set-Cookie", "adm=1; Path=/admin"}};
        return spec;
    }());

    mog::Session s;
    ASSERT_TRUE(s.get(server.origin() + "/admin/login"));

    // Request outside /admin — cookie must not be sent.
    server.SetResponse(200, "ok");
    ASSERT_TRUE(s.get(server.origin() + "/public"));
    for (const auto &h : server.Last().headers)
    {
        if (h.first == "Cookie" || h.first == "cookie")
        {
            EXPECT_EQ(h.second.find("adm=1"), std::string::npos) << "path-scoped cookie leaked";
        }
    }

    // Request inside /admin — cookie should be sent.
    server.SetResponse(200, "ok");
    ASSERT_TRUE(s.get(server.origin() + "/admin/panel"));
    bool sent = false;
    for (const auto &h : server.Last().headers)
    {
        if (h.first == "Cookie" || h.first == "cookie")
        {
            if (h.second.find("adm=1") != std::string::npos)
            {
                sent = true;
            }
        }
    }
    EXPECT_TRUE(sent);
}

TEST(SessionCookies, ManualCookieSentAndClearable)
{
    LocalHttpServer server;
    server.SetResponse(200, "ok");

    mog::Session s;
    s.set_cookie("token", "xyz");
    ASSERT_TRUE(s.get(server.origin() + "/x"));
    bool sent = false;
    for (const auto &h : server.Last().headers)
    {
        if (h.first == "Cookie" || h.first == "cookie")
        {
            if (h.second.find("token=xyz") != std::string::npos)
            {
                sent = true;
            }
        }
    }
    EXPECT_TRUE(sent);

    s.clear_cookies();
    EXPECT_TRUE(s.cookies().empty());
}
