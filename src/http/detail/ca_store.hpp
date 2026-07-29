/**
 * @file ca_store.hpp
 * @brief Hybrid CA trust loading for the embedded mbedTLS backend.
 *
 * Precedence (first successful source wins):
 *   1. Explicit path (CLI / Options::ca_bundle)
 *   2. Environment (MOG_CA_BUNDLE, SSL_CERT_FILE, REQUESTS_CA_BUNDLE, CURL_CA_BUNDLE)
 *   3. System trust (PEM paths; Windows CryptoAPI via dynload)
 *   4. Embedded Mozilla bundle (data/cacert.pem)
 *   5. Fail loud with remediation guidance
 */
#pragma once

#include "mog/error.hpp"

#include <mbedtls/x509_crt.h>
#include <optional>
#include <string>
#include <string_view>

namespace mog::detail
{

/**
 * @brief Describes which trust material was loaded into an mbedTLS chain.
 */
struct CaLoadInfo
{
    /// Machine-readable source tag, e.g. "options", "env:SSL_CERT_FILE", "system:windows",
    /// "embedded:mozilla".
    std::string source;
    /// Human-readable detail (path, store name, bundle date).
    std::string detail;
    /// Number of certificates successfully parsed into the chain.
    int cert_count = 0;
};

/**
 * @brief Load CA certificates into @p chain according to project trust precedence.
 *
 * @param chain Initialized (empty) mbedTLS certificate chain to append into.
 * @param explicit_ca_bundle Optional PEM path from Options / CLI (--cacert).
 * @return Load metadata on success; TlsFailed with guidance when nothing works.
 *
 * Environment override: set @c MOG_NO_EMBEDDED_CA=1 to skip the static Mozilla
 * fallback (useful for forcing system-only policy in tests or lockdown builds).
 */
[[nodiscard]] Result<CaLoadInfo> LoadCaCertificates(
    mbedtls_x509_crt *chain, const std::optional<std::string> &explicit_ca_bundle);

/**
 * @brief Parse a PEM blob into @p chain (appends).
 * @return Number of certs added, or error.
 */
[[nodiscard]] Result<int> ParsePemIntoChain(mbedtls_x509_crt *chain, std::string_view pem,
                                            std::string_view label);

/**
 * @brief Parse a PEM file into @p chain (appends).
 */
[[nodiscard]] Result<int> ParsePemFileIntoChain(mbedtls_x509_crt *chain, const std::string &path);

} // namespace mog::detail
