#!/usr/bin/env bash
set -euo pipefail

xcframework="${1:-}"
expected_version="${2:-}"

if [[ -z "${xcframework}" || -z "${expected_version}" ]]; then
    echo "usage: $0 <mog.xcframework> <expected-version>" >&2
    exit 2
fi

device_library="${xcframework}/ios-arm64/libmog-iphoneos.a"
simulator_library="${xcframework}/ios-arm64_x86_64-simulator/libmog-iphonesimulator.a"

test -f "${xcframework}/Info.plist"
test -f "${device_library}"
test -f "${simulator_library}"

device_arches="$(lipo -archs "${device_library}")"
simulator_arches="$(lipo -archs "${simulator_library}")"
[[ " ${device_arches} " == *" arm64 "* ]]
[[ " ${simulator_arches} " == *" arm64 "* ]]
[[ " ${simulator_arches} " == *" x86_64 "* ]]

for identifier in ios-arm64 ios-arm64_x86_64-simulator; do
    header_root="${xcframework}/${identifier}/Headers/mog"
    test -f "${header_root}/mog.hpp"
    test -f "${header_root}/server.hpp"
    test -f "${header_root}/version.hpp"
    test ! -e "${header_root}/mog_c.h"
done

version_major="${expected_version%%.*}"
version_remainder="${expected_version#*.}"
version_minor="${version_remainder%%.*}"
version_patch="${version_remainder#*.}"
version_patch="${version_patch%%[-.]*}"
version_header="${xcframework}/ios-arm64/Headers/mog/version.hpp"
grep -q "kVersionMajor = ${version_major}" "${version_header}"
grep -q "kVersionMinor = ${version_minor}" "${version_header}"
grep -q "kVersionPatch = ${version_patch}" "${version_header}"

symbols_file="$(mktemp)"
trap 'rm -f "${symbols_file}"' EXIT
nm -g "${device_library}" >"${symbols_file}"
grep -qE '[ST] __?ZN3mog7Version' "${symbols_file}"
grep -qE '[ST] _mbedtls_ssl_init$' "${symbols_file}"

echo "Verified iOS XCFramework ${expected_version}"
echo "  device: ${device_arches}"
echo "  simulator: ${simulator_arches}"
