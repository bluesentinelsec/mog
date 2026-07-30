/**
 * @file embedded_backend_fixture.hpp
 * @brief GoogleTest fixture that pins requests to the embedded backend.
 *
 * The embedded suites (conformance, client integration, keep-alive) validate the
 * embedded backend's own wire behavior, which is no longer the Auto default on
 * platforms with a native backend. gtest_discover_tests runs each test in its own
 * process, so setting MOG_BACKEND here is isolated to that test.
 */
#pragma once

#include <gtest/gtest.h>

#if defined(_WIN32)
#include <stdlib.h>
#else
#include <cstdlib>
#endif

namespace mog::test
{

inline void SetEnvVar(const char *name, const char *value)
{
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

inline void UnsetEnvVar(const char *name)
{
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

/// Forces MOG_BACKEND=embedded for the duration of a test.
class EmbeddedBackend : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        SetEnvVar("MOG_BACKEND", "embedded");
    }
    void TearDown() override
    {
        UnsetEnvVar("MOG_BACKEND");
    }
};

} // namespace mog::test
