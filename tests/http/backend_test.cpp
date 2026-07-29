#include "mog/backend.hpp"

#include <gtest/gtest.h>
#include <stdlib.h>

TEST(BackendTest, ParseNames)
{
    ASSERT_TRUE(mog::ParseBackend("embedded").has_value());
    EXPECT_EQ(*mog::ParseBackend("embedded"), mog::Backend::Embedded);
    ASSERT_TRUE(mog::ParseBackend("AUTO").has_value());
    EXPECT_EQ(*mog::ParseBackend("AUTO"), mog::Backend::Auto);
    ASSERT_TRUE(mog::ParseBackend("curl").has_value());
    EXPECT_EQ(*mog::ParseBackend("curl"), mog::Backend::Curl);
    EXPECT_FALSE(mog::ParseBackend("nope").has_value());
}

TEST(BackendTest, ResolveExplicitOverride)
{
    EXPECT_EQ(mog::ResolveBackend(mog::Backend::Embedded), mog::Backend::Embedded);
}

TEST(BackendTest, DefaultIsEmbedded)
{
    // Clear env for this process if set — restore afterward.
    const char *prev = std::getenv("MOG_BACKEND");
    std::string previous = prev ? prev : "";
#if defined(_WIN32)
    _putenv_s("MOG_BACKEND", "");
#else
    unsetenv("MOG_BACKEND");
#endif
    EXPECT_EQ(mog::ResolveBackend(std::nullopt), mog::Backend::Embedded);
    if (!previous.empty())
    {
#if defined(_WIN32)
        _putenv_s("MOG_BACKEND", previous.c_str());
#else
        setenv("MOG_BACKEND", previous.c_str(), 1);
#endif
    }
}
