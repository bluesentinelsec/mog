/**
 * @file digest_auth_test.cpp
 * @brief Unit + integration tests for HTTP Digest authentication (#8).
 */

#include "http/detail/digest_auth.hpp"
#include "mog/mog.hpp"
#include "test_support/local_http_server.hpp"

#include <gtest/gtest.h>
#include <string>

using mog::detail::BuildDigestAuthorization;
using mog::detail::DigestChallenge;
using mog::detail::ParseDigestChallenge;
using mog::test::LocalHttpServer;

namespace
{

bool Contains(const std::string &haystack, const std::string &needle)
{
    return haystack.find(needle) != std::string::npos;
}

} // namespace

// RFC 2617 §3.5 worked example (MD5, qop=auth).
TEST(DigestAuth, Rfc2617ExampleResponse)
{
    DigestChallenge ch;
    ch.present = true;
    ch.realm = "testrealm@host.com";
    ch.nonce = "dcd98b7102dd2f0e8b11d0f600bfb0c093";
    ch.opaque = "5ccc069c403ebaf9f0171e9517f40e41";
    ch.qop = "auth";
    // algorithm unspecified -> MD5

    const std::string header = BuildDigestAuthorization(ch, "Mufasa", "Circle Of Life", "GET",
                                                        "/dir/index.html", "0a4f113b", 1);

    EXPECT_TRUE(Contains(header, "response=\"6629fae49393a05397450978507c4ef1\"")) << header;
    EXPECT_TRUE(Contains(header, "username=\"Mufasa\""));
    EXPECT_TRUE(Contains(header, "realm=\"testrealm@host.com\""));
    EXPECT_TRUE(Contains(header, "uri=\"/dir/index.html\""));
    EXPECT_TRUE(Contains(header, "qop=auth"));
    EXPECT_TRUE(Contains(header, "nc=00000001"));
    EXPECT_TRUE(Contains(header, "cnonce=\"0a4f113b\""));
    EXPECT_TRUE(Contains(header, "opaque=\"5ccc069c403ebaf9f0171e9517f40e41\""));
}

TEST(DigestAuth, ParsesChallengeWithCommaInQuotedQop)
{
    const auto ch =
        ParseDigestChallenge("Digest realm=\"testrealm@host.com\", qop=\"auth,auth-int\", "
                             "nonce=\"dcd98b7102dd2f0e8b11d0f600bfb0c093\", "
                             "opaque=\"5ccc069c403ebaf9f0171e9517f40e41\"");
    ASSERT_TRUE(ch.present);
    EXPECT_EQ(ch.realm, "testrealm@host.com");
    EXPECT_EQ(ch.nonce, "dcd98b7102dd2f0e8b11d0f600bfb0c093");
    EXPECT_EQ(ch.opaque, "5ccc069c403ebaf9f0171e9517f40e41");
    EXPECT_EQ(ch.qop, "auth,auth-int");

    // Parsed challenge reproduces the RFC response (qop=auth chosen).
    const std::string header = BuildDigestAuthorization(ch, "Mufasa", "Circle Of Life", "GET",
                                                        "/dir/index.html", "0a4f113b", 1);
    EXPECT_TRUE(Contains(header, "response=\"6629fae49393a05397450978507c4ef1\""));
}

TEST(DigestAuth, NonDigestHeaderIsNotPresent)
{
    EXPECT_FALSE(ParseDigestChallenge("Basic realm=\"x\"").present);
    EXPECT_FALSE(ParseDigestChallenge("").present);
}

TEST(DigestAuth, NoQopLegacyOmitsNcAndCnonce)
{
    DigestChallenge ch;
    ch.present = true;
    ch.realm = "r";
    ch.nonce = "n";
    // no qop
    const std::string header = BuildDigestAuthorization(ch, "u", "p", "GET", "/x", "cnonce", 1);
    EXPECT_FALSE(Contains(header, "qop="));
    EXPECT_FALSE(Contains(header, "nc="));
    EXPECT_FALSE(Contains(header, "cnonce="));
    EXPECT_TRUE(Contains(header, "response=\""));
    // Deterministic for identical inputs.
    EXPECT_EQ(header, BuildDigestAuthorization(ch, "u", "p", "GET", "/x", "cnonce", 1));
}

TEST(DigestAuth, Sha256AlgorithmProduces64HexResponse)
{
    DigestChallenge ch;
    ch.present = true;
    ch.realm = "r";
    ch.nonce = "n";
    ch.qop = "auth";
    ch.algorithm = "SHA-256";
    const std::string header = BuildDigestAuthorization(ch, "u", "p", "GET", "/x", "abcd", 1);
    EXPECT_TRUE(Contains(header, "algorithm=SHA-256"));
    const auto pos = header.find("response=\"");
    ASSERT_NE(pos, std::string::npos);
    const auto end = header.find('"', pos + 10);
    ASSERT_NE(end, std::string::npos);
    EXPECT_EQ(end - (pos + 10), 64U); // SHA-256 hex digest length
}

// --- Integration over the loopback server ------------------------------------

TEST(DigestAuth, SessionRetriesWithCredentials)
{
    LocalHttpServer server;
    server.RequireDigestAuth("testrealm", "server-nonce-123");
    server.SetResponse(200, "secret area");

    mog::Options opt;
    mog::WithDigestAuth(opt, "user", "pass");
    auto r = mog::get(server.origin() + "/protected", opt);
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(r->status_code, 200);
    EXPECT_EQ(r->text(), "secret area");

    const auto history = server.History();
    ASSERT_EQ(history.size(), 2U); // initial 401 challenge, then authorized retry

    std::string auth;
    for (const auto &h : history[1].headers)
    {
        if (h.first == "Authorization" || h.first == "authorization")
        {
            auth = h.second;
        }
    }
    EXPECT_EQ(auth.rfind("Digest ", 0), 0U);
    EXPECT_TRUE(Contains(auth, "username=\"user\""));
    EXPECT_TRUE(Contains(auth, "realm=\"testrealm\""));
    EXPECT_TRUE(Contains(auth, "nonce=\"server-nonce-123\""));
    EXPECT_TRUE(Contains(auth, "uri=\"/protected\""));
    EXPECT_TRUE(Contains(auth, "response=\""));
}

TEST(DigestAuth, NoInfiniteRetryOnNonDigestChallenge)
{
    LocalHttpServer server;
    server.RequireAuth("never-matches"); // 401 without a Digest WWW-Authenticate

    mog::Options opt;
    mog::WithDigestAuth(opt, "user", "pass");
    auto r = mog::get(server.origin() + "/x", opt);
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(r->status_code, 401); // returned as-is, no retry loop
}
