/**
 * @file tls.cpp
 * @brief mbedTLS client session over TcpSocket.
 */

#include "http/detail/tls.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mutex>
#include <vector>

namespace mog::detail
{
namespace
{

std::string MbedError(int err)
{
    std::array<char, 256> buf{};
    mbedtls_strerror(err, buf.data(), buf.size());
    return std::string{buf.data()};
}

std::vector<std::string> DefaultCaPaths()
{
    return {
        "/etc/ssl/cert.pem",                    // macOS
        "/etc/ssl/certs/ca-certificates.crt",   // Debian/Ubuntu/etc
        "/etc/pki/tls/certs/ca-bundle.crt",     // RHEL/Fedora
        "/etc/ssl/ca-bundle.pem",               // OpenSUSE
        "/usr/local/etc/openssl/cert.pem",      // Homebrew OpenSSL
        "/opt/homebrew/etc/openssl@3/cert.pem", // Homebrew Apple Silicon
    };
}

struct BioContext
{
    TcpSocket *socket = nullptr;
    std::chrono::milliseconds timeout{std::chrono::seconds(30)};
};

int BioSend(void *ctx, const unsigned char *buf, size_t len)
{
    auto *bio = static_cast<BioContext *>(ctx);
    if (bio == nullptr || bio->socket == nullptr)
    {
        return MBEDTLS_ERR_NET_INVALID_CONTEXT;
    }
    auto result = bio->socket->SendAll(buf, len, bio->timeout);
    if (!result)
    {
        if (result.error().code() == ErrorCode::Timeout)
        {
            return MBEDTLS_ERR_SSL_TIMEOUT;
        }
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    if (*result > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return static_cast<int>(*result);
}

int BioRecv(void *ctx, unsigned char *buf, size_t len)
{
    auto *bio = static_cast<BioContext *>(ctx);
    if (bio == nullptr || bio->socket == nullptr)
    {
        return MBEDTLS_ERR_NET_INVALID_CONTEXT;
    }
    auto result = bio->socket->RecvSome(buf, len, bio->timeout);
    if (!result)
    {
        if (result.error().code() == ErrorCode::Timeout)
        {
            return MBEDTLS_ERR_SSL_TIMEOUT;
        }
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    if (*result == 0)
    {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    if (*result > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    return static_cast<int>(*result);
}

} // namespace

struct TlsSession::Impl
{
    mbedtls_entropy_context entropy{};
    mbedtls_ctr_drbg_context ctr_drbg{};
    mbedtls_ssl_context ssl{};
    mbedtls_ssl_config conf{};
    mbedtls_x509_crt cacert{};
    BioContext bio{};
    bool active = false;

    Impl()
    {
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&ctr_drbg);
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&conf);
        mbedtls_x509_crt_init(&cacert);
    }

    ~Impl()
    {
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
        mbedtls_x509_crt_free(&cacert);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
    }
};

TlsSession::TlsSession() : impl_(std::make_unique<Impl>())
{
}

TlsSession::~TlsSession() = default;

TlsSession::TlsSession(TlsSession &&) noexcept = default;
TlsSession &TlsSession::operator=(TlsSession &&) noexcept = default;

Result<void> TlsSession::Handshake(TcpSocket &socket, std::string_view hostname, bool verify,
                                   const std::optional<std::string> &ca_bundle,
                                   std::chrono::milliseconds timeout)
{
    impl_->bio.socket = &socket;
    impl_->bio.timeout = timeout;

    const char *pers = "mog-tls";
    int ret =
        mbedtls_ctr_drbg_seed(&impl_->ctr_drbg, mbedtls_entropy_func, &impl_->entropy,
                              reinterpret_cast<const unsigned char *>(pers), std::strlen(pers));
    if (ret != 0)
    {
        return Result<void>::Err(Error{ErrorCode::TlsFailed, "ctr_drbg_seed: " + MbedError(ret)});
    }

    ret = mbedtls_ssl_config_defaults(&impl_->conf, MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0)
    {
        return Result<void>::Err(
            Error{ErrorCode::TlsFailed, "ssl_config_defaults: " + MbedError(ret)});
    }

    if (verify)
    {
        mbedtls_ssl_conf_authmode(&impl_->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        bool loaded = false;
        if (ca_bundle.has_value())
        {
            ret = mbedtls_x509_crt_parse_file(&impl_->cacert, ca_bundle->c_str());
            if (ret < 0)
            {
                return Result<void>::Err(
                    Error{ErrorCode::TlsFailed, "failed to load CA bundle: " + MbedError(ret)});
            }
            loaded = true;
        }
        else
        {
            for (const auto &path : DefaultCaPaths())
            {
                ret = mbedtls_x509_crt_parse_file(&impl_->cacert, path.c_str());
                if (ret >= 0)
                {
                    loaded = true;
                    break;
                }
            }
        }
        if (!loaded)
        {
            return Result<void>::Err(Error{ErrorCode::TlsFailed,
                                           "no CA certificates found; set Options::ca_bundle or "
                                           "SSL_CERT_FILE, or disable verify"});
        }
        mbedtls_ssl_conf_ca_chain(&impl_->conf, &impl_->cacert, nullptr);
    }
    else
    {
        mbedtls_ssl_conf_authmode(&impl_->conf, MBEDTLS_SSL_VERIFY_NONE);
    }

    mbedtls_ssl_conf_rng(&impl_->conf, mbedtls_ctr_drbg_random, &impl_->ctr_drbg);
    mbedtls_ssl_conf_read_timeout(&impl_->conf, static_cast<uint32_t>(timeout.count()));

    ret = mbedtls_ssl_setup(&impl_->ssl, &impl_->conf);
    if (ret != 0)
    {
        return Result<void>::Err(Error{ErrorCode::TlsFailed, "ssl_setup: " + MbedError(ret)});
    }

    const std::string host{hostname};
    ret = mbedtls_ssl_set_hostname(&impl_->ssl, host.c_str());
    if (ret != 0)
    {
        return Result<void>::Err(
            Error{ErrorCode::TlsFailed, "ssl_set_hostname: " + MbedError(ret)});
    }

    mbedtls_ssl_set_bio(&impl_->ssl, &impl_->bio, BioSend, BioRecv, nullptr);

    while ((ret = mbedtls_ssl_handshake(&impl_->ssl)) != 0)
    {
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            continue;
        }
        if (ret == MBEDTLS_ERR_SSL_TIMEOUT)
        {
            return Result<void>::Err(Error{ErrorCode::Timeout, "TLS handshake timed out"});
        }
        return Result<void>::Err(Error{ErrorCode::TlsFailed, "handshake: " + MbedError(ret)});
    }

    if (verify)
    {
        const uint32_t flags = mbedtls_ssl_get_verify_result(&impl_->ssl);
        if (flags != 0)
        {
            std::array<char, 512> vrbuf{};
            mbedtls_x509_crt_verify_info(vrbuf.data(), vrbuf.size(), "", flags);
            return Result<void>::Err(Error{
                ErrorCode::TlsFailed, std::string("certificate verify failed: ") + vrbuf.data()});
        }
    }

    impl_->active = true;
    return Result<void>::Ok();
}

Result<std::size_t> TlsSession::Write(const void *data, std::size_t len,
                                      std::chrono::milliseconds timeout)
{
    if (!impl_->active)
    {
        return Result<std::size_t>::Err(Error{ErrorCode::TlsFailed, "TLS session not active"});
    }
    impl_->bio.timeout = timeout;
    const auto *bytes = static_cast<const unsigned char *>(data);
    std::size_t total = 0;
    while (total < len)
    {
        const int ret = mbedtls_ssl_write(&impl_->ssl, bytes + total, len - total);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            continue;
        }
        if (ret == MBEDTLS_ERR_SSL_TIMEOUT)
        {
            return Result<std::size_t>::Err(Error{ErrorCode::Timeout, "TLS write timed out"});
        }
        if (ret < 0)
        {
            return Result<std::size_t>::Err(
                Error{ErrorCode::TlsFailed, "ssl_write: " + MbedError(ret)});
        }
        total += static_cast<std::size_t>(ret);
    }
    return Result<std::size_t>::Ok(total);
}

void TlsSession::AttachSocket(TcpSocket *socket) noexcept
{
    if (impl_)
    {
        impl_->bio.socket = socket;
    }
}

Result<std::size_t> TlsSession::Read(void *data, std::size_t len, std::chrono::milliseconds timeout)
{
    if (!impl_->active)
    {
        return Result<std::size_t>::Err(Error{ErrorCode::TlsFailed, "TLS session not active"});
    }
    impl_->bio.timeout = timeout;
    for (;;)
    {
        const int ret = mbedtls_ssl_read(&impl_->ssl, static_cast<unsigned char *>(data), len);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            continue;
        }
        if (ret == MBEDTLS_ERR_SSL_TIMEOUT)
        {
            return Result<std::size_t>::Err(Error{ErrorCode::Timeout, "TLS read timed out"});
        }
        if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || ret == 0)
        {
            return Result<std::size_t>::Ok(0);
        }
        if (ret < 0)
        {
            return Result<std::size_t>::Err(
                Error{ErrorCode::TlsFailed, "ssl_read: " + MbedError(ret)});
        }
        return Result<std::size_t>::Ok(static_cast<std::size_t>(ret));
    }
}

} // namespace mog::detail
