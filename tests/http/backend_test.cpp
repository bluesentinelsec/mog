#include "http/detail/env.hpp"
#include "http/detail/transport.hpp"
#include "mog/backend.hpp"

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
    ASSERT_TRUE(mog::ParseBackend("fetch").has_value());
    EXPECT_EQ(*mog::ParseBackend("fetch"), mog::Backend::Web);
    EXPECT_FALSE(mog::ParseBackend("nope").has_value());
}

TEST(BackendTest, ResolveExplicitOverride)
{
    EXPECT_EQ(mog::ResolveBackend(mog::Backend::Embedded), mog::Backend::Embedded);
}

TEST(BackendTest, DefaultPrefersNativeWhenAvailable)
{
    // Clear env for this process if set — restore afterward.
    const auto previous = mog::detail::GetEnv("MOG_BACKEND");
    ClearEnv("MOG_BACKEND");
    const auto def = mog::ResolveBackend(std::nullopt);
    const auto native = mog::detail::PreferredNativeBackend();
    if (native != mog::Backend::Embedded && mog::detail::IsBackendAvailable(native))
    {
        EXPECT_EQ(def, native); // Auto prefers the platform-native backend
    }
    else
    {
        EXPECT_EQ(def, mog::Backend::Embedded); // no native available -> fallback
    }
    if (previous.has_value())
    {
        SetEnvVar("MOG_BACKEND", previous->c_str());
    }
}
