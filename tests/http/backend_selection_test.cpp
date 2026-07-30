/**
 * @file backend_selection_test.cpp
 * @brief Backend availability + Auto-fallback selection (#19 slice 1).
 *
 * gtest_discover_tests runs each test in its own process, so registering a fake
 * transport here does not leak into other tests.
 */

#include "http/detail/transport.hpp"
#include "mog/mog.hpp"

#include <gtest/gtest.h>
#include <string>

using mog::Backend;
using mog::detail::IsBackendAvailable;
using mog::detail::PreferredNativeBackend;
using mog::detail::RegisterTransport;
using mog::detail::ResolveAutoBackend;
using mog::detail::Transport;

namespace
{

// A fake native transport that reports available and returns a sentinel response.
class FakeTransport final : public Transport
{
  public:
    [[nodiscard]] std::string_view Name() const noexcept override
    {
        return "fake-native";
    }
    [[nodiscard]] bool Available() const noexcept override
    {
        return true;
    }
    [[nodiscard]] mog::Result<mog::Response> Execute(mog::Method, std::string_view,
                                                     const mog::Options &) override
    {
        mog::Response r;
        r.status_code = 299;
        r.backend = "fake-native";
        return mog::Result<mog::Response>::Ok(std::move(r));
    }
};

} // namespace

TEST(BackendSelection, EmbeddedIsAlwaysAvailable)
{
    EXPECT_TRUE(IsBackendAvailable(Backend::Embedded));
}

TEST(BackendSelection, AutoFallsBackToEmbeddedWhenNoNative)
{
    // Placeholders for native backends report unavailable, so Auto -> Embedded.
    EXPECT_EQ(ResolveAutoBackend(), Backend::Embedded);
}

TEST(BackendSelection, AutoPrefersAvailableNativeBackend)
{
    const Backend native = PreferredNativeBackend();
    if (native == Backend::Embedded)
    {
        GTEST_SKIP() << "no native backend for this platform";
    }
    RegisterTransport(native, std::make_unique<FakeTransport>());

    EXPECT_TRUE(IsBackendAvailable(native));
    EXPECT_EQ(ResolveAutoBackend(), native);

    // A request with Auto (default backend) dispatches to the native transport.
    auto r = mog::get("http://127.0.0.1:9/unused"); // never connects; fake short-circuits
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(r->status_code, 299);
    EXPECT_EQ(r->backend, "fake-native");
}

TEST(BackendSelection, ExplicitEmbeddedIgnoresAvailableNative)
{
    const Backend native = PreferredNativeBackend();
    if (native != Backend::Embedded)
    {
        RegisterTransport(native, std::make_unique<FakeTransport>());
    }
    // Explicitly choosing embedded must not be overridden by an available native.
    mog::Options opt;
    opt.backend = Backend::Embedded;
    EXPECT_EQ(mog::ResolveBackend(opt.backend), Backend::Embedded);
}

TEST(BackendSelection, UnimplementedNativeErrorsWhenSelectedExplicitly)
{
    // Nothing registered over the placeholder: explicit selection must fail loud.
    const Backend native = PreferredNativeBackend();
    if (native == Backend::Embedded)
    {
        GTEST_SKIP() << "no native backend for this platform";
    }
    mog::Options opt;
    opt.backend = native;
    auto r = mog::get("http://127.0.0.1:9/unused", opt);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code(), mog::ErrorCode::UnsupportedBackend);
}
