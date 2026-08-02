/**
 * @file tls_server.cpp
 * @brief Server-side mbedTLS implementation and self-signed certificate generation.
 */
#include "http/detail/tls_server.hpp"

#include "http/detail/mbedtls_threading.hpp"

#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <sstream>
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

// BIO context bridging mbedTLS to our TcpSocket.
struct ServerBio
{
    TcpSocket *socket = nullptr;
    std::chrono::milliseconds timeout{std::chrono::seconds(30)};
};

int BioSend(void *ctx, const unsigned char *buf, size_t len)
{
    auto *bio = static_cast<ServerBio *>(ctx);
    if (bio == nullptr || bio->socket == nullptr)
    {
        return MBEDTLS_ERR_NET_INVALID_CONTEXT;
    }
    auto result = bio->socket->SendAll(buf, len, bio->timeout);
    if (!result)
    {
        return result.error().code() == ErrorCode::Timeout ? MBEDTLS_ERR_SSL_TIMEOUT
                                                           : MBEDTLS_ERR_NET_SEND_FAILED;
    }
    if (*result > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return static_cast<int>(*result);
}

int BioRecv(void *ctx, unsigned char *buf, size_t len)
{
    auto *bio = static_cast<ServerBio *>(ctx);
    if (bio == nullptr || bio->socket == nullptr)
    {
        return MBEDTLS_ERR_NET_INVALID_CONTEXT;
    }
    auto result = bio->socket->RecvSome(buf, len, bio->timeout);
    if (!result)
    {
        return result.error().code() == ErrorCode::Timeout ? MBEDTLS_ERR_SSL_TIMEOUT
                                                           : MBEDTLS_ERR_NET_RECV_FAILED;
    }
    // After a readable wait, zero bytes means the peer closed the connection.
    // Treating it as a reset (rather than WANT_READ) avoids a busy loop on EOF.
    if (*result == 0)
    {
        return MBEDTLS_ERR_NET_CONN_RESET;
    }
    if (*result > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    return static_cast<int>(*result);
}

Result<std::string> ReadFile(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return Result<std::string>::Err(Error{ErrorCode::FileError, "cannot open: " + path});
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return Result<std::string>::Ok(ss.str());
}

} // namespace

// ---------------------------------------------------------------------------
// TlsServerContext
// ---------------------------------------------------------------------------

Result<TlsServerContext> TlsServerContext::FromPem(std::string cert_pem, std::string key_pem,
                                                   std::string key_password)
{
    if (cert_pem.empty() || key_pem.empty())
    {
        return Result<TlsServerContext>::Err(
            Error{ErrorCode::InvalidArgument, "TLS certificate and key must not be empty"});
    }
    TlsServerContext ctx;
    ctx.cert_pem_ = std::move(cert_pem);
    ctx.key_pem_ = std::move(key_pem);
    ctx.key_password_ = std::move(key_password);
    return Result<TlsServerContext>::Ok(std::move(ctx));
}

Result<TlsServerContext> TlsServerContext::FromFiles(const std::string &cert_path,
                                                     const std::string &key_path,
                                                     const std::string &key_password)
{
    auto cert = ReadFile(cert_path);
    if (!cert)
    {
        return Result<TlsServerContext>::Err(cert.error());
    }
    auto key = ReadFile(key_path);
    if (!key)
    {
        return Result<TlsServerContext>::Err(key.error());
    }
    return FromPem(std::move(*cert), std::move(*key), key_password);
}

Result<TlsServerContext> TlsServerContext::SelfSigned(const std::string &common_name)
{
    EnsureMbedtlsThreading(); // must precede any mbedtls_*_init (see Impl ctor)
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_pk_context key;
    mbedtls_x509write_cert crt;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_pk_init(&key);
    mbedtls_x509write_crt_init(&crt);

    struct Guard
    {
        mbedtls_entropy_context *e;
        mbedtls_ctr_drbg_context *d;
        mbedtls_pk_context *k;
        mbedtls_x509write_cert *c;
        ~Guard()
        {
            mbedtls_x509write_crt_free(c);
            mbedtls_pk_free(k);
            mbedtls_ctr_drbg_free(d);
            mbedtls_entropy_free(e);
        }
    } guard{&entropy, &ctr_drbg, &key, &crt};

    const char *pers = "mog-selfsigned";
    int ret =
        mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                              reinterpret_cast<const unsigned char *>(pers), std::strlen(pers));
    if (ret != 0)
    {
        return Result<TlsServerContext>::Err(
            Error{ErrorCode::TlsFailed, "ctr_drbg_seed: " + MbedError(ret)});
    }

    // Generate an EC P-256 key.
    ret = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
    if (ret != 0)
    {
        return Result<TlsServerContext>::Err(
            Error{ErrorCode::TlsFailed, "pk_setup: " + MbedError(ret)});
    }
    ret = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key), mbedtls_ctr_drbg_random,
                              &ctr_drbg);
    if (ret != 0)
    {
        return Result<TlsServerContext>::Err(
            Error{ErrorCode::TlsFailed, "ec_gen_key: " + MbedError(ret)});
    }

    // Write the private key PEM.
    std::array<unsigned char, 4096> key_buf{};
    ret = mbedtls_pk_write_key_pem(&key, key_buf.data(), key_buf.size());
    if (ret != 0)
    {
        return Result<TlsServerContext>::Err(
            Error{ErrorCode::TlsFailed, "write key pem: " + MbedError(ret)});
    }
    std::string key_pem(reinterpret_cast<char *>(key_buf.data()));

    // Build a self-signed certificate.
    const std::string subject = "CN=" + common_name;
    mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&crt, &key);
    mbedtls_x509write_crt_set_issuer_key(&crt, &key);
    ret = mbedtls_x509write_crt_set_subject_name(&crt, subject.c_str());
    if (ret != 0)
    {
        return Result<TlsServerContext>::Err(
            Error{ErrorCode::TlsFailed, "set subject: " + MbedError(ret)});
    }
    ret = mbedtls_x509write_crt_set_issuer_name(&crt, subject.c_str());
    if (ret != 0)
    {
        return Result<TlsServerContext>::Err(
            Error{ErrorCode::TlsFailed, "set issuer: " + MbedError(ret)});
    }

    const unsigned char serial[] = {0x01};
    ret = mbedtls_x509write_crt_set_serial_raw(&crt, const_cast<unsigned char *>(serial),
                                               sizeof(serial));
    if (ret != 0)
    {
        return Result<TlsServerContext>::Err(
            Error{ErrorCode::TlsFailed, "set serial: " + MbedError(ret)});
    }
    // Fixed, wide validity window so generation does not depend on the clock.
    ret = mbedtls_x509write_crt_set_validity(&crt, "20200101000000", "20401231235959");
    if (ret != 0)
    {
        return Result<TlsServerContext>::Err(
            Error{ErrorCode::TlsFailed, "set validity: " + MbedError(ret)});
    }
    mbedtls_x509write_crt_set_basic_constraints(&crt, 0, -1);

    std::array<unsigned char, 4096> crt_buf{};
    ret = mbedtls_x509write_crt_pem(&crt, crt_buf.data(), crt_buf.size(), mbedtls_ctr_drbg_random,
                                    &ctr_drbg);
    if (ret != 0)
    {
        return Result<TlsServerContext>::Err(
            Error{ErrorCode::TlsFailed, "write cert pem: " + MbedError(ret)});
    }
    std::string cert_pem(reinterpret_cast<char *>(crt_buf.data()));

    return FromPem(std::move(cert_pem), std::move(key_pem), {});
}

// ---------------------------------------------------------------------------
// TlsServerSession
// ---------------------------------------------------------------------------

struct TlsServerSession::Impl
{
    mbedtls_entropy_context entropy{};
    mbedtls_ctr_drbg_context ctr_drbg{};
    mbedtls_ssl_context ssl{};
    mbedtls_ssl_config conf{};
    mbedtls_x509_crt cert{};
    mbedtls_pk_context key{};
    ServerBio bio{};
    bool active = false;

    Impl()
    {
        // Register mbedTLS threading before any context init (see the client
        // TlsSession::Impl for why: MBEDTLS_THREADING_ALT mutexes on Windows).
        EnsureMbedtlsThreading();
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&ctr_drbg);
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&conf);
        mbedtls_x509_crt_init(&cert);
        mbedtls_pk_init(&key);
    }

    ~Impl()
    {
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
        mbedtls_x509_crt_free(&cert);
        mbedtls_pk_free(&key);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
    }
};

TlsServerSession::TlsServerSession() : impl_(std::make_unique<Impl>())
{
}

TlsServerSession::~TlsServerSession() = default;
TlsServerSession::TlsServerSession(TlsServerSession &&) noexcept = default;
TlsServerSession &TlsServerSession::operator=(TlsServerSession &&) noexcept = default;

Result<void> TlsServerSession::Handshake(TcpSocket &socket, const TlsServerContext &context,
                                         std::chrono::milliseconds timeout)
{
    impl_->bio.socket = &socket;
    impl_->bio.timeout = timeout;

    const char *pers = "mog-tls-server";
    int ret =
        mbedtls_ctr_drbg_seed(&impl_->ctr_drbg, mbedtls_entropy_func, &impl_->entropy,
                              reinterpret_cast<const unsigned char *>(pers), std::strlen(pers));
    if (ret != 0)
    {
        return Result<void>::Err(Error{ErrorCode::TlsFailed, "ctr_drbg_seed: " + MbedError(ret)});
    }

    // Parse the certificate chain (buffer length must include the trailing NUL).
    const std::string &cert_pem = context.cert_pem();
    ret = mbedtls_x509_crt_parse(&impl_->cert,
                                 reinterpret_cast<const unsigned char *>(cert_pem.c_str()),
                                 cert_pem.size() + 1);
    if (ret != 0)
    {
        return Result<void>::Err(
            Error{ErrorCode::TlsFailed, "parse certificate: " + MbedError(ret)});
    }

    const std::string &key_pem = context.key_pem();
    const std::string &pwd = context.key_password();
    ret = mbedtls_pk_parse_key(
        &impl_->key, reinterpret_cast<const unsigned char *>(key_pem.c_str()), key_pem.size() + 1,
        pwd.empty() ? nullptr : reinterpret_cast<const unsigned char *>(pwd.c_str()), pwd.size(),
        mbedtls_ctr_drbg_random, &impl_->ctr_drbg);
    if (ret != 0)
    {
        return Result<void>::Err(Error{ErrorCode::TlsFailed, "parse key: " + MbedError(ret)});
    }

    ret = mbedtls_ssl_config_defaults(&impl_->conf, MBEDTLS_SSL_IS_SERVER,
                                      MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0)
    {
        return Result<void>::Err(
            Error{ErrorCode::TlsFailed, "ssl_config_defaults: " + MbedError(ret)});
    }

    mbedtls_ssl_conf_rng(&impl_->conf, mbedtls_ctr_drbg_random, &impl_->ctr_drbg);
    mbedtls_ssl_conf_read_timeout(&impl_->conf, static_cast<uint32_t>(timeout.count()));
    mbedtls_ssl_conf_authmode(&impl_->conf, MBEDTLS_SSL_VERIFY_NONE); // no client cert required

    ret = mbedtls_ssl_conf_own_cert(&impl_->conf, &impl_->cert, &impl_->key);
    if (ret != 0)
    {
        return Result<void>::Err(Error{ErrorCode::TlsFailed, "conf_own_cert: " + MbedError(ret)});
    }

    ret = mbedtls_ssl_setup(&impl_->ssl, &impl_->conf);
    if (ret != 0)
    {
        return Result<void>::Err(Error{ErrorCode::TlsFailed, "ssl_setup: " + MbedError(ret)});
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

    impl_->active = true;
    return Result<void>::Ok();
}

Result<std::size_t> TlsServerSession::Write(const void *data, std::size_t len,
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

Result<std::size_t> TlsServerSession::Read(void *data, std::size_t len,
                                           std::chrono::milliseconds timeout)
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
