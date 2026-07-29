/**
 * @file mozilla_ca_bundle.hpp
 * @brief Embedded Mozilla CA root bundle (generated from data/cacert.pem).
 */
#pragma once

#include <string_view>

namespace mog::detail
{

/**
 * @brief Full PEM text of the embedded Mozilla CA roots (curl cacert format).
 */
[[nodiscard]] std::string_view EmbeddedMozillaCaPem() noexcept;

/**
 * @brief Human-readable Mozilla bundle timestamp from the PEM header.
 */
[[nodiscard]] std::string_view EmbeddedMozillaCaBundleDate() noexcept;

/**
 * @brief SHA-256 (hex) of the source PEM used to generate the embed.
 */
[[nodiscard]] std::string_view EmbeddedMozillaCaSha256() noexcept;

} // namespace mog::detail
