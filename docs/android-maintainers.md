---
title: Android Build and CI
description: "Maintain the mog Android NDK build, Prefab AAR, emulator tests, CI job, and release asset."
---

# Android build and CI

This page is for mog maintainers. Android application developers should start
with the public [Android guide](android.html).

## Toolchain

The checked-in Gradle wrapper pins Gradle 8.10.2. The Android project pins:

- Android Gradle Plugin 8.7.3;
- JDK 17;
- compile SDK 35 and minimum API 21;
- NDK 27.2.12479018; and
- CMake 3.22.1 with the shared LLVM libc++ runtime.

Set `ANDROID_HOME` to an Android SDK and accept the SDK licenses. Gradle installs
the pinned SDK, NDK, and CMake packages when they are missing. An emulator test
also needs `adb` and an x86_64 system image; CI uses API 30.

## Build and package locally

Use the repository wrappers for the standard workflow:

```bash
make android          # Debug + Release AARs and consumer APKs
make android-package  # versioned Release AAR under build/android/
```

The direct Release command is:

```bash
./android/gradlew -p android --no-daemon \
  :mog:assembleRelease
./android/gradlew -p android --no-daemon \
  :test-app:assembleRelease
```

The unversioned Gradle output is
`android/mog/build/outputs/aar/mog-release.aar`. `make android-package` copies it
to `build/android/mog-android-release-<version>.aar` without changing its
contents.

The `:mog` module asks the root CMake project to build the Android `mog` shared
target. That target whole-archive links `mog_lib`, mbedTLS, and miniz into
`libmog.so`. Gradle publishes it as a Prefab module and generates the AAR for
`armeabi-v7a`, `arm64-v8a`, and `x86_64`. A Gradle task stages public headers and
generates `mog/version.hpp` from the root `VERSION` file and CMake template.

Do not hand-edit a second version. The root `VERSION` file remains the only
version source.

## Run the Android tests

Start a compatible emulator or connect a device, then run:

```bash
make android-test
```

The default test APK targets x86_64. For a local arm64 emulator, build it with:

```bash
./android/gradlew -p android --no-daemon \
  :mog:assembleRelease
./android/gradlew -p android --no-daemon \
  -PmogTestAbi=arm64-v8a :test-app:assembleRelease
bash scripts/run_android_tests.sh
```

The test application is deliberately a separate Prefab consumer. Its Gradle
module depends on the generated `mog-release.aar` file, not on the `:mog`
project. Its CMake project calls `find_package(mog CONFIG REQUIRED)` and links
only `mog::mog`; it does not include headers from the repository source tree or
link the internal `mog_lib` target. Build the AAR first, as shown above.

On device, `tests/android/android_test.cpp` verifies:

- the generated version API is exported through the AAR;
- Android `Auto` resolves to the embedded backend;
- an actual HTTP request to an in-process loopback `mog::Server`; and
- an actual mbedTLS HTTPS request to a self-signed loopback server.

The tests require the manifest `INTERNET` permission but never use the public
network. `scripts/run_android_tests.sh` installs the APK, starts its Activity,
and treats the `MOG_ANDROID_TESTS: PASS` logcat sentinel as success.

Run the full native `make test` suite before submitting changes as well. It is
the comprehensive behavioral contract; the emulator suite proves the Android
toolchain, Prefab consumer boundary, sockets, threads, and TLS path.

## CI coverage

The `android` job in `.github/workflows/ci.yml`:

1. builds Debug AAR and consumer APK variants;
2. builds the Release AAR and consumer APK;
3. inspects the Release AAR for all supported ABIs, Prefab headers, and the
   version from `VERSION`;
4. boots an x86_64 API 30 emulator and runs the Release package tests; and
5. uploads `mog-android-release-<version>.aar` as a workflow artifact.

Compilation and the consumer link are separate checks. A successful AAR build
alone does not prove that a downstream CMake target can import or link it.

## Release deployment

The `build-android` job in `.github/workflows/release.yml` repeats the Release
build, package validation, and emulator test. It stages the versioned AAR as a
workflow artifact. The publish job depends on that job and attaches the AAR
directly to the GitHub software release alongside the native and Emscripten
packages.

GitHub Pages needs no Android-specific deployment step. The docs workflow
builds every `docs/*.md` page, so this guide and the public Android page deploy
with the rest of the documentation after changes reach the deployment branch.

## Maintainer checklist

When Android behavior or packaging changes:

1. update `docs/android.md` for caller-visible changes;
2. update the device test when transport or package behavior changes;
3. run `make android-test` and `make test`;
4. run `make android-package` and inspect the AAR's `prefab/`, `jni/`, and ABI
   entries; and
5. keep the Gradle, NDK, CMake, and emulator versions synchronized between the
   build files, this page, and CI.
