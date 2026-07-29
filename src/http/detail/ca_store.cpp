/**
 * @file ca_store.cpp
 * @brief Hybrid CA trust: options/env → system → embedded Mozilla.
 */

#include "http/detail/ca_store.hpp"

#include "http/detail/env.hpp"
#include "http/detail/mozilla_ca_bundle.hpp"
#include "mog/dynload.hpp"
#include "mog/log.hpp"

#include <filesystem>
#include <mbedtls/error.h>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// CryptoAPI types only — crypt32.dll is loaded via SharedLibrary at runtime.
#include <wincrypt.h>
#endif

namespace mog::detail
{
namespace
{

std::string GuidanceSuffix()
{
    return " Set --cacert / Options::ca_bundle, SSL_CERT_FILE, or MOG_CA_BUNDLE; "
           "or install OS CA certificates. Embedded Mozilla roots are used when "
           "no system bundle is found (disable with MOG_NO_EMBEDDED_CA=1).";
}

bool EnvDisablesEmbedded()
{
    auto v = GetEnv("MOG_NO_EMBEDDED_CA");
    if (!v)
    {
        return false;
    }
    return *v == "1" || *v == "true" || *v == "TRUE" || *v == "yes" || *v == "YES";
}

void ResetChain(mbedtls_x509_crt *chain)
{
    mbedtls_x509_crt_free(chain);
    mbedtls_x509_crt_init(chain);
}

int CountCerts(const mbedtls_x509_crt *chain)
{
    int count = 0;
    for (const mbedtls_x509_crt *c = chain; c != nullptr; c = c->next)
    {
        if (c->raw.p != nullptr)
        {
            ++count;
        }
    }
    return count;
}

std::string MbedErr(int ret)
{
    char buf[160];
    mbedtls_strerror(ret, buf, sizeof(buf));
    return std::string{buf};
}

std::vector<std::string> DefaultSystemPemPaths()
{
    return {
        "/etc/ssl/cert.pem",
        "/etc/ssl/certs/ca-certificates.crt",
        "/etc/pki/tls/certs/ca-bundle.crt",
        "/etc/ssl/ca-bundle.pem",
        "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
        "/usr/local/etc/openssl/cert.pem",
        "/usr/local/etc/openssl@3/cert.pem",
        "/opt/homebrew/etc/openssl@3/cert.pem",
        "/opt/homebrew/etc/openssl/cert.pem",
    };
}

Result<CaLoadInfo> FinishIfNonEmpty(mbedtls_x509_crt *chain, std::string source, std::string detail)
{
    const int count = CountCerts(chain);
    if (count <= 0)
    {
        return Result<CaLoadInfo>::Err(
            Error{ErrorCode::TlsFailed, "CA source produced no certificates: " + source});
    }
    CaLoadInfo info;
    info.source = std::move(source);
    info.detail = std::move(detail);
    info.cert_count = count;
    return Result<CaLoadInfo>::Ok(std::move(info));
}

#if defined(_WIN32)
Result<CaLoadInfo> LoadWindowsSystemStore(mbedtls_x509_crt *chain)
{
    auto lib = SharedLibrary::Load("crypt32.dll");
    if (!lib)
    {
        return Result<CaLoadInfo>::Err(lib.error());
    }

    using CertOpenSystemStoreW_t = HCERTSTORE(WINAPI *)(HCRYPTPROV_LEGACY, LPCWSTR);
    using CertEnumCertificatesInStore_t = PCCERT_CONTEXT(WINAPI *)(HCERTSTORE, PCCERT_CONTEXT);
    using CertCloseStore_t = BOOL(WINAPI *)(HCERTSTORE, DWORD);

    auto open = lib->Symbol<CertOpenSystemStoreW_t>("CertOpenSystemStoreW");
    auto enumerate = lib->Symbol<CertEnumCertificatesInStore_t>("CertEnumCertificatesInStore");
    auto close = lib->Symbol<CertCloseStore_t>("CertCloseStore");
    if (!open || !enumerate || !close)
    {
        return Result<CaLoadInfo>::Err(Error{ErrorCode::DynamicLibraryError,
                                             "crypt32.dll missing required CryptoAPI symbols"});
    }

    int parsed_ok = 0;
    const wchar_t *stores[] = {L"ROOT", L"CA"};
    for (const wchar_t *store_name : stores)
    {
        // MSVC: HCRYPTPROV_LEGACY is an integer handle type; nullptr does not convert.
        HCERTSTORE store = (*open)(static_cast<HCRYPTPROV_LEGACY>(0), store_name);
        if (store == nullptr)
        {
            continue;
        }
        PCCERT_CONTEXT ctx = nullptr;
        while ((ctx = (*enumerate)(store, ctx)) != nullptr)
        {
            if (ctx->pbCertEncoded == nullptr || ctx->cbCertEncoded == 0)
            {
                continue;
            }
            const int ret = mbedtls_x509_crt_parse(chain, ctx->pbCertEncoded, ctx->cbCertEncoded);
            if (ret == 0)
            {
                ++parsed_ok;
            }
        }
        (*close)(store, 0);
    }

    if (parsed_ok <= 0)
    {
        return Result<CaLoadInfo>::Err(Error{
            ErrorCode::TlsFailed, "Windows system certificate stores yielded no usable roots"});
    }
    return FinishIfNonEmpty(chain, "system:windows",
                            "CryptoAPI ROOT+CA (" + std::to_string(parsed_ok) + " certs)");
}
#endif

Result<CaLoadInfo> LoadFromEnv(mbedtls_x509_crt *chain)
{
    static const char *kVars[] = {"MOG_CA_BUNDLE", "SSL_CERT_FILE", "REQUESTS_CA_BUNDLE",
                                  "CURL_CA_BUNDLE"};
    for (const char *var : kVars)
    {
        auto path = GetEnv(var);
        if (!path)
        {
            continue;
        }
        ResetChain(chain);
        auto n = ParsePemFileIntoChain(chain, *path);
        if (n)
        {
            return FinishIfNonEmpty(chain, std::string("env:") + var, *path);
        }
        MOG_LOG_DEBUG("ca_store: {}={} failed: {}", var, *path, n.error().message());
    }

    if (auto dir_list = GetEnv("SSL_CERT_DIR"))
    {
        ResetChain(chain);
        std::string dirs = *dir_list;
        std::size_t start = 0;
        int total = 0;
        std::string first_dir;
        while (start <= dirs.size())
        {
            const std::size_t sep = dirs.find(':', start);
            const std::string dir =
                dirs.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
            start = sep == std::string::npos ? dirs.size() + 1 : sep + 1;
            if (dir.empty())
            {
                continue;
            }
            namespace fs = std::filesystem;
            std::error_code ec;
            if (!fs::is_directory(dir, ec))
            {
                continue;
            }
            for (const auto &entry : fs::directory_iterator(dir, ec))
            {
                if (ec || !entry.is_regular_file(ec))
                {
                    continue;
                }
                const int before = CountCerts(chain);
                auto n = ParsePemFileIntoChain(chain, entry.path().string());
                if (n)
                {
                    total += CountCerts(chain) - before;
                    if (first_dir.empty())
                    {
                        first_dir = dir;
                    }
                }
                else
                {
                    // parse_file failure may leave chain unchanged for hard errors
                    (void)n;
                }
            }
        }
        if (total > 0)
        {
            return FinishIfNonEmpty(chain, "env:SSL_CERT_DIR",
                                    first_dir.empty() ? *dir_list : first_dir);
        }
        ResetChain(chain);
    }

    return Result<CaLoadInfo>::Err(Error{ErrorCode::TlsFailed, "no CA material from environment"});
}

Result<CaLoadInfo> LoadFromSystem(mbedtls_x509_crt *chain)
{
#if defined(_WIN32)
    ResetChain(chain);
    auto win = LoadWindowsSystemStore(chain);
    if (win)
    {
        return win;
    }
    MOG_LOG_DEBUG("ca_store: Windows system store unavailable: {}", win.error().message());
    ResetChain(chain);
#endif

    for (const auto &path : DefaultSystemPemPaths())
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::is_regular_file(path, ec))
        {
            continue;
        }
        ResetChain(chain);
        auto n = ParsePemFileIntoChain(chain, path);
        if (n)
        {
            return FinishIfNonEmpty(chain, "system:pem", path);
        }
        MOG_LOG_DEBUG("ca_store: system path {} failed: {}", path, n.error().message());
        ResetChain(chain);
    }

    return Result<CaLoadInfo>::Err(Error{ErrorCode::TlsFailed, "no system CA certificates found"});
}

Result<CaLoadInfo> LoadEmbedded(mbedtls_x509_crt *chain)
{
    if (EnvDisablesEmbedded())
    {
        return Result<CaLoadInfo>::Err(
            Error{ErrorCode::TlsFailed, "embedded CA disabled via MOG_NO_EMBEDDED_CA"});
    }
    ResetChain(chain);
    const auto pem = EmbeddedMozillaCaPem();
    auto n = ParsePemIntoChain(chain, pem, "embedded:mozilla");
    if (!n)
    {
        return Result<CaLoadInfo>::Err(n.error());
    }
    return FinishIfNonEmpty(chain, "embedded:mozilla",
                            std::string("Mozilla CA bundle as of ") +
                                std::string{EmbeddedMozillaCaBundleDate()});
}

} // namespace

Result<int> ParsePemIntoChain(mbedtls_x509_crt *chain, std::string_view pem, std::string_view label)
{
    if (chain == nullptr || pem.empty())
    {
        return Result<int>::Err(
            Error{ErrorCode::InvalidArgument, "empty PEM for " + std::string{label}});
    }
    const int before = CountCerts(chain);
    std::string owned{pem};
    const auto *bytes = reinterpret_cast<const unsigned char *>(owned.data());
    // PEM parse requires a trailing NUL included in the length.
    const int ret = mbedtls_x509_crt_parse(chain, bytes, owned.size() + 1);
    if (ret < 0)
    {
        return Result<int>::Err(
            Error{ErrorCode::TlsFailed, std::string("failed to parse CA PEM (") +
                                            std::string{label} + "): " + MbedErr(ret)});
    }
    const int after = CountCerts(chain);
    const int added = after - before;
    if (added <= 0)
    {
        return Result<int>::Err(
            Error{ErrorCode::TlsFailed,
                  std::string("no certificates in PEM (") + std::string{label} + ")"});
    }
    return Result<int>::Ok(added);
}

Result<int> ParsePemFileIntoChain(mbedtls_x509_crt *chain, const std::string &path)
{
    if (chain == nullptr || path.empty())
    {
        return Result<int>::Err(Error{ErrorCode::InvalidArgument, "CA path is empty"});
    }
    const int before = CountCerts(chain);
    const int ret = mbedtls_x509_crt_parse_file(chain, path.c_str());
    if (ret < 0)
    {
        return Result<int>::Err(
            Error{ErrorCode::TlsFailed, "failed to load CA file '" + path + "': " + MbedErr(ret)});
    }
    const int after = CountCerts(chain);
    const int added = after - before;
    if (added <= 0)
    {
        return Result<int>::Err(
            Error{ErrorCode::TlsFailed, "CA file contained no certificates: " + path});
    }
    return Result<int>::Ok(added);
}

Result<CaLoadInfo> LoadCaCertificates(mbedtls_x509_crt *chain,
                                      const std::optional<std::string> &explicit_ca_bundle)
{
    if (chain == nullptr)
    {
        return Result<CaLoadInfo>::Err(
            Error{ErrorCode::InvalidArgument, "CA chain pointer is null"});
    }

    // 1) Explicit CLI / Options path — hard fail if provided and unreadable.
    if (explicit_ca_bundle.has_value() && !explicit_ca_bundle->empty())
    {
        ResetChain(chain);
        auto n = ParsePemFileIntoChain(chain, *explicit_ca_bundle);
        if (!n)
        {
            return Result<CaLoadInfo>::Err(
                Error{ErrorCode::TlsFailed,
                      "failed to load explicit CA bundle '" + *explicit_ca_bundle +
                          "': " + std::string{n.error().message()} + "." + GuidanceSuffix()});
        }
        auto info = FinishIfNonEmpty(chain, "options", *explicit_ca_bundle);
        if (info)
        {
            MOG_LOG_DEBUG("ca_store: loaded {} certs from options ({})", info->cert_count,
                          info->detail);
        }
        return info;
    }

    // 2) Environment overrides
    {
        auto env = LoadFromEnv(chain);
        if (env)
        {
            MOG_LOG_DEBUG("ca_store: loaded {} certs from {} ({})", env->cert_count, env->source,
                          env->detail);
            return env;
        }
        MOG_LOG_DEBUG("ca_store: env CA not used: {}", env.error().message());
        ResetChain(chain);
    }

    // 3) System trust store / PEM paths
    {
        auto sys = LoadFromSystem(chain);
        if (sys)
        {
            MOG_LOG_DEBUG("ca_store: loaded {} certs from {} ({})", sys->cert_count, sys->source,
                          sys->detail);
            return sys;
        }
        MOG_LOG_DEBUG("ca_store: system CA not used: {}", sys.error().message());
        ResetChain(chain);
    }

    // 4) Embedded Mozilla fallback (minimal containers / missing OS bundle)
    {
        auto emb = LoadEmbedded(chain);
        if (emb)
        {
            MOG_LOG_INFO("ca_store: using embedded Mozilla CA roots ({}, {} certs)", emb->detail,
                         emb->cert_count);
            return emb;
        }
        MOG_LOG_DEBUG("ca_store: embedded CA not used: {}", emb.error().message());
        ResetChain(chain);
    }

    // 5) Fail loud
    return Result<CaLoadInfo>::Err(Error{
        ErrorCode::TlsFailed,
        std::string("no CA certificates available for TLS verification.") + GuidanceSuffix()});
}

} // namespace mog::detail
