/**
 * @file ca_store_test.cpp
 * @brief Unit tests for hybrid CA trust loading.
 */

#include "http/detail/ca_store.hpp"
#include "http/detail/mozilla_ca_bundle.hpp"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <gtest/gtest.h>
#include <mbedtls/x509_crt.h>
#include <string>

namespace
{

class TempPemFile
{
  public:
    explicit TempPemFile(std::string_view contents)
    {
        path_ = "mog_test_ca_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)) + ".pem";
        std::ofstream out(path_, std::ios::binary);
        out << contents;
    }

    ~TempPemFile()
    {
        std::remove(path_.c_str());
    }

    [[nodiscard]] const std::string &path() const
    {
        return path_;
    }

  private:
    std::string path_;
};

// Minimal self-signed-looking single cert is heavy; reuse a slice of the embedded
// bundle (first cert only) for file-path tests.
std::string FirstPemCertFromEmbedded()
{
    const auto pem = mog::detail::EmbeddedMozillaCaPem();
    const auto begin = pem.find("-----BEGIN CERTIFICATE-----");
    const auto end = pem.find("-----END CERTIFICATE-----");
    if (begin == std::string_view::npos || end == std::string_view::npos)
    {
        return {};
    }
    return std::string{pem.substr(
               begin, end - begin + std::string_view{"-----END CERTIFICATE-----"}.size())} +
           "\n";
}

} // namespace

TEST(CaStore, EmbeddedMozillaParses)
{
    const auto pem = mog::detail::EmbeddedMozillaCaPem();
    ASSERT_FALSE(pem.empty());
    EXPECT_NE(pem.find("BEGIN CERTIFICATE"), std::string_view::npos);
    EXPECT_FALSE(mog::detail::EmbeddedMozillaCaBundleDate().empty());
    EXPECT_EQ(mog::detail::EmbeddedMozillaCaSha256().size(), 64u);

    mbedtls_x509_crt chain;
    mbedtls_x509_crt_init(&chain);
    auto n = mog::detail::ParsePemIntoChain(&chain, pem, "test");
    ASSERT_TRUE(n) << n.error().to_string();
    EXPECT_GT(*n, 50); // Mozilla bundle has well over 50 roots
    mbedtls_x509_crt_free(&chain);
}

TEST(CaStore, LoadExplicitFile)
{
    const std::string one = FirstPemCertFromEmbedded();
    ASSERT_FALSE(one.empty());
    TempPemFile file(one);

    mbedtls_x509_crt chain;
    mbedtls_x509_crt_init(&chain);
    auto info = mog::detail::LoadCaCertificates(&chain, file.path());
    ASSERT_TRUE(info) << info.error().to_string();
    EXPECT_EQ(info->source, "options");
    EXPECT_EQ(info->detail, file.path());
    EXPECT_GE(info->cert_count, 1);
    mbedtls_x509_crt_free(&chain);
}

TEST(CaStore, ExplicitMissingFileFailsLoud)
{
    mbedtls_x509_crt chain;
    mbedtls_x509_crt_init(&chain);
    auto info = mog::detail::LoadCaCertificates(&chain, std::string{"/no/such/mog_ca_bundle.pem"});
    ASSERT_FALSE(info);
    EXPECT_EQ(info.error().code(), mog::ErrorCode::TlsFailed);
    EXPECT_NE(info.error().message().find("cacert"), std::string_view::npos);
    mbedtls_x509_crt_free(&chain);
}

TEST(CaStore, LoadCaCertificatesFindsSomeTrust)
{
    // On developer machines and CI: system PEM or embedded Mozilla must succeed.
    mbedtls_x509_crt chain;
    mbedtls_x509_crt_init(&chain);
    auto info = mog::detail::LoadCaCertificates(&chain, std::nullopt);
    ASSERT_TRUE(info) << info.error().to_string();
    EXPECT_FALSE(info->source.empty());
    EXPECT_GT(info->cert_count, 0);
    // Source is one of options/env/system/embedded.
    EXPECT_TRUE(info->source.find("system") != std::string::npos ||
                info->source.find("embedded") != std::string::npos ||
                info->source.find("env") != std::string::npos);
    mbedtls_x509_crt_free(&chain);
}
