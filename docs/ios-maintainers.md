---
title: iOS Build and CI
description: "Maintain the mog iOS device and Simulator builds, XCFramework, package tests, CI job, and release asset."
---

# iOS build and CI

This page is for mog maintainers. Application developers should start with the
public [iOS guide](ios.html).

## Toolchain and package contract

The iOS build requires macOS with Xcode command-line tools and CMake 3.28 or
newer. The package has an iOS 13 deployment target and contains:

- `ios-arm64` for physical devices; and
- `ios-arm64_x86_64-simulator` for Apple Silicon and Intel Simulator hosts.

The package uses Apple's libc++ and is not code signed. The consuming
application owns its deployment target, signing identity, entitlements, and
bundle lifecycle.

## Build and package locally

Use the repository wrappers for the standard workflow:

```bash
make ios          # Debug + Release XCFrameworks and consumer app builds
make ios-package  # versioned Release XCFramework zip under build/ios/release
```

The direct Release command is:

```bash
scripts/build_ios_xcframework.sh Release build/ios/release
```

It produces:

```text
build/ios/release/mog.xcframework/
build/ios/release/mog-ios-xcframework-release-<version>.zip
```

The script configures separate Xcode builds for `iphoneos` arm64 and
`iphonesimulator` arm64/x86_64. `mog_lib`, mbedTLS, miniz, and the small mbedTLS
support archives are static libraries, so the script combines them into one
self-contained archive per platform before calling
`xcodebuild -create-xcframework`. Only public `mog/...` headers are staged.

`scripts/verify_ios_xcframework.sh` checks the platform identifiers,
architectures, public and generated headers, version constants, core mog
symbols, and embedded TLS symbols. The root `VERSION` file remains the only
version source.

## Build and run the package tests

The test application is deliberately a separate XCFramework consumer. Build it
with:

```bash
scripts/build_ios_test_apps.sh \
  build/ios/release/mog.xcframework \
  Release build/ios/consumer-release
```

That command builds one unsigned device app and one unsigned Simulator app. Its
CMake project requires CMake 3.28, links the XCFramework path and Foundation,
and relies on the XCFramework to supply the selected library and headers. It
does not include the repository's headers or link the internal `mog_lib`
target.

Run the Simulator app with:

```bash
BUILD_DIR=build/ios/consumer-release/iphonesimulator \
  scripts/run_ios_tests.sh
```

Or run the complete Release flow with `make ios-test`. The runner creates a
temporary iPhone Simulator, installs the app, follows its structured log, and
treats `MOG_IOS_TEST_RESULT: 0` as success. It shuts down and removes the
temporary Simulator afterward.

The app verifies from the packaged library:

- the generated version API;
- iOS `Auto` selection of the NSURLSession backend;
- an actual native HTTP request to an in-process loopback `mog::Server`;
- an explicit embedded-backend request, proving the folded static dependencies;
- capability-aware proxy fallback and explicit-native rejection; and
- an actual HTTPS request to a self-signed loopback `mog::Server`.

The suite never uses the public network. Its test-only `Info.plist` permits
loopback HTTP so ATS does not obscure transport testing.

## CI coverage

The `ios` job in `.github/workflows/ci.yml`:

1. builds and verifies a Debug XCFramework;
2. links Debug device and Simulator consumer apps;
3. builds and verifies the Release XCFramework;
4. links Release device and Simulator consumer apps;
5. boots an iPhone Simulator and runs the Release package tests; and
6. uploads the versioned Release XCFramework zip as a workflow artifact.

Compilation, package construction, consumer linking, and runtime behavior are
separate checks. A successful source archive build alone does not prove that
Xcode or CMake can select and consume the packaged slices.

## Release deployment

The `build-ios` job in `.github/workflows/release.yml` repeats the Release
package build, slice/version validation, device and Simulator consumer links,
and Simulator test. The publish job depends on it and attaches
`mog-ios-xcframework-release-<version>.zip` to the GitHub software release
alongside the desktop, Android, and Emscripten packages.

GitHub Pages needs no iOS-specific deployment step. The docs workflow builds
every `docs/*.md` page, so this guide and the public iOS page deploy with the
rest of the documentation after changes reach `main`.

## Maintainer checklist

When iOS behavior or packaging changes:

1. update `docs/ios.md` for caller-visible changes;
2. update the Simulator app when transport or package behavior changes;
3. run `make ios-test` and the native `make test` suite;
4. inspect the XCFramework identifiers, headers, and both archive slices; and
5. keep the deployment target and CI commands synchronized between the build
   scripts, this page, and the workflows.
