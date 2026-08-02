// Tests for `mog serve` argument parsing and ServerOptions mapping.
// The blocking RunServe orchestration is exercised via the server library tests;
// here we verify the pure parse + mapping so it stays deterministic.

#include <gtest/gtest.h>
#include <mog/cli.hpp>
#include <string>
#include <vector>

namespace
{

mog::Result<mog::cli::ServeArgs> ParseServe(std::vector<std::string> argv_strings)
{
    std::vector<char *> argv;
    argv.reserve(argv_strings.size());
    for (auto &s : argv_strings)
    {
        argv.push_back(s.data());
    }
    return mog::cli::ParseServeArgv(static_cast<int>(argv.size()), argv.data());
}

} // namespace

TEST(ServeCli, DefaultsWhenNoArgs)
{
    auto a = ParseServe({"mog", "serve"});
    ASSERT_TRUE(a) << (a ? "" : a.error().to_string());
    EXPECT_EQ(a->directory, ".");
    EXPECT_EQ(a->bind_address, "127.0.0.1");
    EXPECT_EQ(a->port, 8000);
    EXPECT_FALSE(a->self_signed);
    EXPECT_FALSE(a->no_listing);
}

TEST(ServeCli, ParsesDirectoryAndOptions)
{
    auto a = ParseServe({"mog", "serve", "/srv/www", "--port", "9443", "--bind", "0.0.0.0",
                         "--threads", "4", "--no-listing"});
    ASSERT_TRUE(a) << (a ? "" : a.error().to_string());
    EXPECT_EQ(a->directory, "/srv/www");
    EXPECT_EQ(a->port, 9443);
    EXPECT_EQ(a->bind_address, "0.0.0.0");
    EXPECT_EQ(a->threads, 4u);
    EXPECT_TRUE(a->no_listing);
}

TEST(ServeCli, ParsesSelfSignedFlag)
{
    auto a = ParseServe({"mog", "serve", "--self-signed"});
    ASSERT_TRUE(a);
    EXPECT_TRUE(a->self_signed);
}

TEST(ServeCli, BuildOptionsPlainHttp)
{
    mog::cli::ServeArgs args;
    args.port = 8080;
    args.bind_address = "0.0.0.0";
    args.threads = 2;
    auto opt = mog::cli::BuildServeOptions(args);
    ASSERT_TRUE(opt) << (opt ? "" : opt.error().to_string());
    EXPECT_EQ(opt->port, 8080);
    EXPECT_EQ(opt->bind_address, "0.0.0.0");
    EXPECT_EQ(opt->threads, 2u);
    EXPECT_FALSE(opt->tls.enabled);
}

TEST(ServeCli, BuildOptionsSelfSignedEnablesTls)
{
    mog::cli::ServeArgs args;
    args.self_signed = true;
    auto opt = mog::cli::BuildServeOptions(args);
    ASSERT_TRUE(opt) << (opt ? "" : opt.error().to_string());
    EXPECT_TRUE(opt->tls.enabled);
    EXPECT_FALSE(opt->tls.cert_pem.empty());
    EXPECT_FALSE(opt->tls.key_pem.empty());
}

TEST(ServeCli, BuildOptionsRejectsSelfSignedWithFiles)
{
    mog::cli::ServeArgs args;
    args.self_signed = true;
    args.tls_cert = "cert.pem";
    args.tls_key = "key.pem";
    auto opt = mog::cli::BuildServeOptions(args);
    EXPECT_FALSE(opt);
}

TEST(ServeCli, BuildOptionsRejectsCertWithoutKey)
{
    mog::cli::ServeArgs args;
    args.tls_cert = "cert.pem";
    auto opt = mog::cli::BuildServeOptions(args);
    EXPECT_FALSE(opt);
}
