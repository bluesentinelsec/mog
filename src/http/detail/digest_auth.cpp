/**
 * @file digest_auth.cpp
 * @brief HTTP Digest access authentication (RFC 2617 / RFC 7616).
 */

#include "http/detail/digest_auth.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <mbedtls/md5.h>
#include <mbedtls/sha256.h>
#include <random>
#include <string>

namespace mog::detail
{
namespace
{

bool StartsWithIgnoreCase(std::string_view text, std::string_view prefix)
{
    if (text.size() < prefix.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(text[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i])))
        {
            return false;
        }
    }
    return true;
}

std::string_view Trim(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
    {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
    {
        s.remove_suffix(1);
    }
    return s;
}

std::string ToHex(const unsigned char *data, std::size_t len)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i)
    {
        out.push_back(kHex[data[i] >> 4]);
        out.push_back(kHex[data[i] & 0x0F]);
    }
    return out;
}

// True when the (base) algorithm is SHA-256; everything else defaults to MD5.
bool IsSha256(std::string_view algorithm)
{
    return StartsWithIgnoreCase(algorithm, "SHA-256");
}

bool IsSessVariant(std::string_view algorithm)
{
    return algorithm.size() >= 5 &&
           StartsWithIgnoreCase(algorithm.substr(algorithm.size() - 5), "-sess");
}

std::string Hash(std::string_view algorithm, const std::string &input)
{
    const auto *bytes = reinterpret_cast<const unsigned char *>(input.data());
    if (IsSha256(algorithm))
    {
        std::array<unsigned char, 32> out{};
        mbedtls_sha256(bytes, input.size(), out.data(), 0);
        return ToHex(out.data(), out.size());
    }
    std::array<unsigned char, 16> out{};
    mbedtls_md5(bytes, input.size(), out.data());
    return ToHex(out.data(), out.size());
}

std::string NcHex(unsigned nc)
{
    std::array<char, 9> buf{};
    std::snprintf(buf.data(), buf.size(), "%08x", nc);
    return std::string{buf.data()};
}

} // namespace

DigestChallenge ParseDigestChallenge(std::string_view www_authenticate)
{
    DigestChallenge challenge;
    std::string_view rest = Trim(www_authenticate);
    if (!StartsWithIgnoreCase(rest, "Digest"))
    {
        return challenge; // present=false
    }
    rest.remove_prefix(std::string_view{"Digest"}.size());
    rest = Trim(rest);
    challenge.present = true;

    // Parse comma-separated key=value pairs, honoring quoted values (which may
    // themselves contain commas, e.g. qop="auth,auth-int").
    std::size_t i = 0;
    while (i < rest.size())
    {
        // key
        const std::size_t key_start = i;
        while (i < rest.size() && rest[i] != '=' && rest[i] != ',')
        {
            ++i;
        }
        std::string_view key = Trim(rest.substr(key_start, i - key_start));
        std::string value;
        if (i < rest.size() && rest[i] == '=')
        {
            ++i; // skip '='
            if (i < rest.size() && rest[i] == '"')
            {
                ++i; // opening quote
                const std::size_t vstart = i;
                while (i < rest.size() && rest[i] != '"')
                {
                    ++i;
                }
                value = std::string{rest.substr(vstart, i - vstart)};
                if (i < rest.size())
                {
                    ++i; // closing quote
                }
            }
            else
            {
                const std::size_t vstart = i;
                while (i < rest.size() && rest[i] != ',')
                {
                    ++i;
                }
                value = std::string{Trim(rest.substr(vstart, i - vstart))};
            }
        }
        if (i < rest.size() && rest[i] == ',')
        {
            ++i; // skip separator
        }
        while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t'))
        {
            ++i;
        }

        if (key == "realm")
        {
            challenge.realm = std::move(value);
        }
        else if (key == "nonce")
        {
            challenge.nonce = std::move(value);
        }
        else if (key == "opaque")
        {
            challenge.opaque = std::move(value);
        }
        else if (key == "qop")
        {
            challenge.qop = std::move(value);
        }
        else if (key == "algorithm")
        {
            challenge.algorithm = std::move(value);
        }
    }
    return challenge;
}

std::string BuildDigestAuthorization(const DigestChallenge &challenge, std::string_view username,
                                     std::string_view password, std::string_view method,
                                     std::string_view uri, std::string_view cnonce, unsigned nc)
{
    const std::string &algo = challenge.algorithm;

    std::string ha1 =
        Hash(algo, std::string{username} + ":" + challenge.realm + ":" + std::string{password});
    if (IsSessVariant(algo))
    {
        ha1 = Hash(algo, ha1 + ":" + challenge.nonce + ":" + std::string{cnonce});
    }
    const std::string ha2 = Hash(algo, std::string{method} + ":" + std::string{uri});

    // Offer qop=auth when the server lists it (auth-int is unsupported).
    const bool use_qop = challenge.qop.find("auth") != std::string::npos;

    std::string response;
    if (use_qop)
    {
        response = Hash(algo, ha1 + ":" + challenge.nonce + ":" + NcHex(nc) + ":" +
                                  std::string{cnonce} + ":auth:" + ha2);
    }
    else
    {
        response = Hash(algo, ha1 + ":" + challenge.nonce + ":" + ha2);
    }

    std::string out;
    out += "username=\"" + std::string{username} + "\"";
    out += ", realm=\"" + challenge.realm + "\"";
    out += ", nonce=\"" + challenge.nonce + "\"";
    out += ", uri=\"" + std::string{uri} + "\"";
    out += ", response=\"" + response + "\"";
    if (!algo.empty())
    {
        out += ", algorithm=" + algo;
    }
    if (use_qop)
    {
        out += ", qop=auth";
        out += ", nc=" + NcHex(nc);
        out += ", cnonce=\"" + std::string{cnonce} + "\"";
    }
    if (!challenge.opaque.empty())
    {
        out += ", opaque=\"" + challenge.opaque + "\"";
    }
    return out;
}

std::string GenerateCnonce()
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<int> dist(0, 15);
    std::string cnonce;
    cnonce.reserve(16);
    for (int i = 0; i < 16; ++i)
    {
        cnonce.push_back(kHex[dist(gen)]);
    }
    return cnonce;
}

} // namespace mog::detail
