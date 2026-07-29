#include "mog/backend.hpp"

#include "http/detail/env.hpp"

#include <gtest/gtest.h>

#if defined(_WIN32)
#include <stdlib.h>
#else
#include <stdlib.h>
#endif

namespace
{

void ClearEnv(const char *name)
{
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

void SetEnvVar(const char *name, const char *value)
{
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

} // namespace

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
    const auto previous = mog::detail::GetEnv("MOG_BACKEND");
    ClearEnv("MOG_BACKEND");
    EXPECT_EQ(mog::ResolveBackend(std::nullopt), mog::Backend::Embedded);
    if (previous.has_value())
    {
        SetEnvVar("MOG_BACKEND", previous->c_str());
    }
}
