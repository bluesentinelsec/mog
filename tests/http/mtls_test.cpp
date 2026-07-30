/**
 * @file mtls_test.cpp
 * @brief Tests for the client-certificate (mTLS) plumbing (#8).
 *
 * A full mTLS handshake needs a TLS server that requests a client cert, which is
 * beyond the plain-HTTP loopback fixture (issue #8: "tests where practical").
 * These tests cover the client-cert load path and error surface: a bad path is
 * reported clearly, and a valid cert/key pair loads (the later failure against a
 * non-TLS endpoint is a handshake error, not a load error).
 */

#include "mog/cli.hpp"
#include "mog/mog.hpp"
#include "test_support/local_http_server.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

using mog::test::LocalHttpServer;

namespace
{

// Throwaway self-signed EC cert/key pair (prime256v1) for load-path testing only.
constexpr const char *kTestCert = R"(-----BEGIN CERTIFICATE-----
MIIBijCCAS+gAwIBAgIUQ89B9iUHxkc55HvI38LTTPxdXB8wCgYIKoZIzj0EAwIw
GjEYMBYGA1UEAwwPbW9nLXRlc3QtY2xpZW50MB4XDTI2MDczMDAxMjQyN1oXDTM2
MDcyNzAxMjQyN1owGjEYMBYGA1UEAwwPbW9nLXRlc3QtY2xpZW50MFkwEwYHKoZI
zj0CAQYIKoZIzj0DAQcDQgAEljbbPHGI/SLqUCB2i9hsB2TFUKzCPwXQd3oCZE/l
j7N4Dc6ezq/XZyEKKtEEp3IJhzIsU3mLg1BuayPpH1hC8qNTMFEwHQYDVR0OBBYE
FC8LQygzQ6krusSPMSQTutu9RjrGMB8GA1UdIwQYMBaAFC8LQygzQ6krusSPMSQT
utu9RjrGMA8GA1UdEwEB/wQFMAMBAf8wCgYIKoZIzj0EAwIDSQAwRgIhAPw6U1Er
6/WLayHY4xkI3z4P4Vy5PTgzxNqp7INibBBIAiEAoJ6ZbPqapudpvKi93lMXB4m4
8rEUGVBEJjb0XJqQoOE=
-----END CERTIFICATE-----
)";

constexpr const char *kTestKey = R"(-----BEGIN EC PRIVATE KEY-----
MHcCAQEEIPt5MrcertmhNcp6W8yqngMGBH6c8onbOmdgekjZhrjZoAoGCCqGSM49
AwEHoUQDQgAEljbbPHGI/SLqUCB2i9hsB2TFUKzCPwXQd3oCZE/lj7N4Dc6ezq/X
ZyEKKtEEp3IJhzIsU3mLg1BuayPpH1hC8g==
-----END EC PRIVATE KEY-----
)";

std::string WriteTemp(const std::string &name, const char *contents)
{
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream out(path, std::ios::binary);
    out << contents;
    return path.string();
}

// An https URL pointing at the plain-HTTP loopback server: the TCP connect
// succeeds, so the TLS handshake (and client-cert loading) is exercised.
std::string HttpsOrigin(const LocalHttpServer &server)
{
    return "https://127.0.0.1:" + std::to_string(server.port()) + "/";
}

} // namespace

TEST(Mtls, MissingClientCertReportsClearError)
{
    LocalHttpServer server;
    server.SetResponse(200, "ok");

    mog::Options opt;
    opt.verify_tls = false;
    opt.backend = mog::Backend::Embedded; // validates the embedded backend's mTLS load path
    opt.timeout = std::chrono::seconds(2);
    opt.client_cert = "/no/such/client-cert.pem";

    auto r = mog::get(HttpsOrigin(server), opt);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code(), mog::ErrorCode::TlsFailed);
    EXPECT_NE(std::string{r.error().message()}.find("client certificate"), std::string::npos)
        << r.error().to_string();
}

TEST(Mtls, MissingClientKeyReportsClearError)
{
    LocalHttpServer server;
    server.SetResponse(200, "ok");
    const std::string cert = WriteTemp("mog_mtls_cert_only.pem", kTestCert);

    mog::Options opt;
    opt.verify_tls = false;
    opt.backend = mog::Backend::Embedded; // validates the embedded backend's mTLS load path
    opt.timeout = std::chrono::seconds(2);
    opt.client_cert = cert;
    opt.client_key = "/no/such/client-key.pem";

    auto r = mog::get(HttpsOrigin(server), opt);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code(), mog::ErrorCode::TlsFailed);
    EXPECT_NE(std::string{r.error().message()}.find("client key"), std::string::npos)
        << r.error().to_string();

    std::filesystem::remove(cert);
}

TEST(Mtls, ValidCertAndKeyLoadSuccessfully)
{
    LocalHttpServer server;
    server.SetResponse(200, "ok");
    const std::string cert = WriteTemp("mog_mtls_cert.pem", kTestCert);
    const std::string key = WriteTemp("mog_mtls_key.pem", kTestKey);

    mog::Options opt;
    opt.verify_tls = false;
    opt.backend = mog::Backend::Embedded; // validates the embedded backend's mTLS load path
    opt.timeout = std::chrono::seconds(2);
    opt.client_cert = cert;
    opt.client_key = key;

    // The plain-HTTP server is not a TLS peer, so the handshake still fails — but
    // NOT during cert/key loading, proving the material parsed and configured.
    auto r = mog::get(HttpsOrigin(server), opt);
    ASSERT_FALSE(r);
    const std::string msg{r.error().message()};
    EXPECT_EQ(msg.find("load client certificate"), std::string::npos) << msg;
    EXPECT_EQ(msg.find("load client key"), std::string::npos) << msg;

    std::filesystem::remove(cert);
    std::filesystem::remove(key);
}

TEST(Mtls, CliMapsCertKeyPassOptions)
{
    mog::cli::Args args;
    args.url = "https://example.com/";
    args.client_cert = "/tmp/c.pem";
    args.client_key = "/tmp/k.pem";
    args.client_key_password = "secret";

    auto prepared = mog::cli::PrepareRequest(args);
    ASSERT_TRUE(prepared) << prepared.error().to_string();
    ASSERT_TRUE(prepared->options.client_cert.has_value());
    EXPECT_EQ(*prepared->options.client_cert, "/tmp/c.pem");
    ASSERT_TRUE(prepared->options.client_key.has_value());
    EXPECT_EQ(*prepared->options.client_key, "/tmp/k.pem");
    EXPECT_EQ(prepared->options.client_key_password, "secret");
}
