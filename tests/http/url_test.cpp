#include "http/detail/url.hpp"

#include <gtest/gtest.h>

using mog::detail::AppendQuery;
using mog::detail::ParseUrl;

TEST(UrlTest, ParsesHttpsDefaultPort)
{
    auto url = ParseUrl("https://example.com/path?q=1");
    ASSERT_TRUE(url);
    EXPECT_EQ(url->scheme, "https");
    EXPECT_EQ(url->host, "example.com");
    EXPECT_EQ(url->port, 443);
    EXPECT_EQ(url->path, "/path");
    EXPECT_EQ(url->query, "q=1");
}

TEST(UrlTest, ParsesHttpExplicitPort)
{
    auto url = ParseUrl("http://localhost:8080/");
    ASSERT_TRUE(url);
    EXPECT_EQ(url->scheme, "http");
    EXPECT_EQ(url->host, "localhost");
    EXPECT_EQ(url->port, 8080);
    EXPECT_EQ(url->path, "/");
}

TEST(UrlTest, RejectsMissingScheme)
{
    auto url = ParseUrl("example.com/foo");
    ASSERT_FALSE(url);
    EXPECT_EQ(url.error().code(), mog::ErrorCode::InvalidUrl);
}

TEST(UrlTest, AppendQuery)
{
    EXPECT_EQ(AppendQuery("https://example.com/x", {{"a", "b"}}), "https://example.com/x?a=b");
    EXPECT_EQ(AppendQuery("https://example.com/x?q=1", {{"a", "b c"}}),
              "https://example.com/x?q=1&a=b%20c");
}
