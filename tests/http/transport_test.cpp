/**
 * @file transport_test.cpp
 * @brief Transport registry open/closed dispatch tests.
 */

#include "http/detail/transport.hpp"
#include "mog/http.hpp"
#include "mog/options.hpp"

#include <gtest/gtest.h>
#include <memory>

namespace
{

class FakeTransport final : public mog::detail::Transport
{
  public:
    [[nodiscard]] std::string_view Name() const noexcept override
    {
        return "fake";
    }

    [[nodiscard]] mog::Result<mog::Response> Execute(mog::Method, std::string_view url,
                                                     const mog::Options &) override
    {
        mog::Response r;
        r.status_code = 299;
        r.url = std::string{url};
        r.backend = "fake";
        r.body = "from-fake";
        return mog::Result<mog::Response>::Ok(std::move(r));
    }
};

} // namespace

TEST(TransportRegistry, EmbeddedIsRegistered)
{
    mog::detail::EnsureDefaultTransportsRegistered();
    auto *t = mog::detail::FindTransport(mog::Backend::Embedded);
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(t->Name(), "embedded");
}

TEST(TransportRegistry, UnimplementedCurl)
{
    mog::detail::EnsureDefaultTransportsRegistered();
    auto *t = mog::detail::FindTransport(mog::Backend::Curl);
    ASSERT_NE(t, nullptr);
    auto r = t->Execute(mog::Method::Get, "https://example.com", {});
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code(), mog::ErrorCode::UnsupportedBackend);
}

TEST(TransportRegistry, CanReplaceWithCustomTransport)
{
    // Load defaults first, then replace curl (open for extension without editing request()).
    mog::detail::EnsureDefaultTransportsRegistered();
    mog::detail::RegisterTransport(mog::Backend::Curl, std::make_unique<FakeTransport>());
    auto *t = mog::detail::FindTransport(mog::Backend::Curl);
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(t->Name(), "fake");

    mog::Options opt;
    opt.backend = mog::Backend::Curl;
    auto r = mog::request(mog::Method::Get, "https://example.com/x", opt);
    ASSERT_TRUE(r) << r.error().to_string();
    EXPECT_EQ(r->status_code, 299);
    EXPECT_EQ(r->body, "from-fake");
    EXPECT_EQ(r->backend, "fake");

    // Restore unimplemented stub so later tests see a clear error for curl.
    class Unimpl final : public mog::detail::Transport
    {
      public:
        std::string_view Name() const noexcept override
        {
            return "curl";
        }
        mog::Result<mog::Response> Execute(mog::Method, std::string_view,
                                           const mog::Options &) override
        {
            return mog::Result<mog::Response>::Err(
                mog::Error{mog::ErrorCode::UnsupportedBackend,
                           "backend 'curl' is not implemented yet; use embedded (default)"});
        }
    };
    mog::detail::RegisterTransport(mog::Backend::Curl, std::make_unique<Unimpl>());
}
