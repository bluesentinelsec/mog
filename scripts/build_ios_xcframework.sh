#!/usr/bin/env bash
set -euo pipefail

repository_root="$(cd "$(dirname "$0")/.." && pwd)"
configuration="${1:-Release}"
configuration_lower="$(printf '%s' "${configuration}" | tr '[:upper:]' '[:lower:]')"
output_root="${2:-${repository_root}/build/ios/${configuration_lower}}"
deployment_target="${MOG_IOS_DEPLOYMENT_TARGET:-13.0}"

case "${configuration}" in
    Debug|Release) ;;
    *)
        echo "Configuration must be Debug or Release (got '${configuration}')" >&2
        exit 2
        ;;
esac

if [[ "${output_root}" != /* ]]; then
    output_root="${repository_root}/${output_root}"
fi

version="$(tr -d '[:space:]' <"${repository_root}/VERSION" | sed 's/^v//;s/^V//;s/#.*//')"
device_build="${output_root}/iphoneos"
simulator_build="${output_root}/iphonesimulator"
combined_root="${output_root}/combined"
headers_root="${output_root}/headers"
xcframework="${output_root}/mog.xcframework"
archive="${output_root}/mog-ios-xcframework-${configuration_lower}-${version}.zip"

for tool in cmake xcodebuild xcrun libtool lipo ditto; do
    command -v "${tool}" >/dev/null 2>&1 || {
        echo "Required Apple build tool is missing: ${tool}" >&2
        exit 2
    }
done

cmake -E remove_directory "${output_root}"
cmake -E make_directory "${combined_root}"

configure_and_build() {
    local sdk="$1"
    local architectures="$2"
    local build_dir="$3"

    cmake -S "${repository_root}" -B "${build_dir}" -G Xcode \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_SYSROOT="${sdk}" \
        -DCMAKE_OSX_ARCHITECTURES="${architectures}" \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="${deployment_target}" \
        -DCMAKE_XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET="${deployment_target}" \
        -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
        -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED=NO \
        -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY= \
        -DMOG_BUILD_APP=OFF \
        -DMOG_BUILD_TESTS=OFF \
        -DMOG_BUILD_BENCHMARKS=OFF \
        -DMOG_BUILD_C_SHARED=OFF \
        -DMOG_WITH_CLI11=OFF \
        -DMOG_WITH_JSON=OFF \
        -DMOG_WITH_SPDLOG=OFF

    cmake --build "${build_dir}" --config "${configuration}" --target mog_lib --parallel
}

combine_archives() {
    local build_dir="$1"
    local destination="$2"
    local archives=()
    local name
    local candidate

    for name in libmog.a libmbedtls.a libmbedx509.a libmbedcrypto.a libminiz.a; do
        candidate="$(find "${build_dir}" -type f -name "${name}" -path "*/${configuration}/*" -print -quit)"
        if [[ -z "${candidate}" ]]; then
            echo "Missing ${name} in ${build_dir}" >&2
            exit 1
        fi
        archives+=("${candidate}")
    done

    for name in libeverest.a libp256m.a; do
        candidate="$(find "${build_dir}" -type f -name "${name}" -path "*/${configuration}/*" -print -quit)"
        if [[ -n "${candidate}" ]]; then
            archives+=("${candidate}")
        fi
    done

    libtool -static -o "${destination}" "${archives[@]}"
}

configure_and_build iphoneos arm64 "${device_build}"
configure_and_build iphonesimulator 'arm64;x86_64' "${simulator_build}"

combine_archives "${device_build}" "${combined_root}/libmog-iphoneos.a"
combine_archives "${simulator_build}" "${combined_root}/libmog-iphonesimulator.a"

cmake -E make_directory "${headers_root}/mog"
cmake -E copy_directory "${repository_root}/include/mog" "${headers_root}/mog"
cmake -E rm -f "${headers_root}/mog/mog_c.h"
cmake -E copy "${device_build}/generated/include/mog/version.hpp" \
    "${headers_root}/mog/version.hpp"

xcodebuild -create-xcframework \
    -library "${combined_root}/libmog-iphoneos.a" \
    -headers "${headers_root}" \
    -library "${combined_root}/libmog-iphonesimulator.a" \
    -headers "${headers_root}" \
    -output "${xcframework}"

ditto -c -k --norsrc --noextattr --keepParent "${xcframework}" "${archive}"

"${repository_root}/scripts/verify_ios_xcframework.sh" "${xcframework}" "${version}"

echo "XCFramework: ${xcframework}"
echo "Package: ${archive}"
