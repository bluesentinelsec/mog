/**
 * @file digest_auth.hpp
 * @brief HTTP Digest access authentication (RFC 2617 / RFC 7616) for the embedded backend.
 *
 * Supports the common cases: qop="auth" (and no-qop legacy), algorithm MD5 /
 * MD5-sess / SHA-256 / SHA-256-sess. qop=auth-int is not supported.
 */
#pragma once

#include <string>
#include <string_view>

namespace mog::detail
{

/**
 * @brief Parsed WWW-Authenticate Digest challenge.
 */
struct DigestChallenge
{
    bool present = false; ///< True only when the header is a Digest challenge.
    std::string realm;
    std::string nonce;
    std::string opaque;
    std::string qop;       ///< Raw qop value ("auth", "auth,auth-int", …); empty = none.
    std::string algorithm; ///< e.g. "MD5", "SHA-256", "MD5-sess"; empty defaults to MD5.
};

/**
 * @brief Parse a WWW-Authenticate header value.
 * @return A challenge with @c present=false when @p www_authenticate is not Digest.
 */
[[nodiscard]] DigestChallenge ParseDigestChallenge(std::string_view www_authenticate);

/**
 * @brief Build an Authorization header value ("Digest …") answering @p challenge.
 *
 * @param cnonce Client nonce (supply @ref GenerateCnonce in production; a fixed
 *               value in tests). @param nc request counter (1 on first use).
 */
[[nodiscard]] std::string BuildDigestAuthorization(const DigestChallenge &challenge,
                                                   std::string_view username,
                                                   std::string_view password,
                                                   std::string_view method, std::string_view uri,
                                                   std::string_view cnonce, unsigned nc = 1);

/**
 * @brief Generate a random hex client nonce.
 */
[[nodiscard]] std::string GenerateCnonce();

} // namespace mog::detail
