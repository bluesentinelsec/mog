/**
 * @file web_test.cpp
 * @brief Browser-runtime tests for the Emscripten Fetch transport.
 */

#include "http/detail/transport.hpp"
#include "mog/backend.hpp"
#include "mog/http.hpp"
#include "mog/server.hpp"

#include <cstdlib>
#include <emscripten.h>
#include <gtest/gtest.h>
#include <string>

namespace
{

std::string FixtureUrl()
{
    if (const char *configured = std::getenv("MOG_WEB_TEST_ORIGIN"); configured != nullptr)
    {
        return std::string{configured} + "/web_fixture.txt";
    }
    const char *origin =
        emscripten_run_script_string("typeof window !== 'undefined' ? window.location.origin : "
                                     "process.env.MOG_WEB_TEST_ORIGIN");
    return std::string{origin != nullptr ? origin : ""} + "/web_fixture.txt";
}

} // namespace

TEST(WebBackend, IsAutoSelectedAndAvailable)
{
    EXPECT_EQ(mog::detail::PreferredNativeBackend(), mog::Backend::Web);
    EXPECT_EQ(mog::ResolveBackend(), mog::Backend::Web);
    EXPECT_TRUE(mog::detail::IsBackendAvailable(mog::Backend::Web));
    EXPECT_FALSE(mog::detail::IsBackendAvailable(mog::Backend::Embedded));
    EXPECT_EQ(mog::ParseBackend("fetch"), mog::Backend::Web);
    EXPECT_EQ(mog::ToString(mog::Backend::Web), "web");
}

TEST(WebBackend, FetchesSameOriginResponse)
{
    auto response = mog::get(FixtureUrl());
    ASSERT_TRUE(response) << response.error().to_string();
    EXPECT_EQ(response->status_code, 200);
    EXPECT_EQ(response->body, "mog browser fetch works\n");
    EXPECT_EQ(response->downloaded_bytes, response->body.size());
    EXPECT_EQ(response->backend, "web");
    EXPECT_FALSE(response->content_type().empty());
}

TEST(WebBackend, EnforcesResponseLimit)
{
    mog::Options options;
    options.max_response_bytes = 4;
    auto response = mog::get(FixtureUrl(), options);
    ASSERT_FALSE(response);
    EXPECT_EQ(response.error().code(), mog::ErrorCode::ResponseTooLarge);
}

TEST(WebBackend, RejectsBrowserControlledTlsOptions)
{
    mog::Options options;
    options.verify_tls = false;
    auto response = mog::get(FixtureUrl(), options);
    ASSERT_FALSE(response);
    EXPECT_EQ(response.error().code(), mog::ErrorCode::InvalidArgument);
}

TEST(WebServer, FailsClearly)
{
    mog::Server server;
    auto started = server.start();
    ASSERT_FALSE(started);
    EXPECT_EQ(started.error().code(), mog::ErrorCode::UnsupportedBackend);
    EXPECT_FALSE(server.running());
    EXPECT_EQ(server.port(), 0);
}
