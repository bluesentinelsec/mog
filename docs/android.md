---
title: Android
description: "Use mog from Android NDK applications through the release Prefab AAR."
---

# Android

The Android release is a native C++20 HTTP/S client and server packaged as a
multi-ABI [Prefab](https://developer.android.com/build/native-dependencies) AAR.
It exposes mog's normal C++ API to an app's CMake build; it does not add a
Java/Kotlin or JNI wrapper API.

Android API 21 or newer is supported. The AAR contains `armeabi-v7a`,
`arm64-v8a`, and `x86_64` libraries.

## Add the AAR to an application

Download `mog-android-release-<version>.aar` from the
[GitHub release](https://github.com/bluesentinelsec/mog/releases) and copy it to
the application's `app/libs` directory. Enable Prefab, select the shared C++
runtime, and add the local AAR in the app module's `build.gradle`:

```groovy
android {
    defaultConfig {
        minSdk 21
        externalNativeBuild {
            cmake {
                arguments "-DANDROID_STL=c++_shared"
            }
        }
    }

    buildFeatures {
        prefab true
    }

    externalNativeBuild {
        cmake {
            path file("src/main/cpp/CMakeLists.txt")
        }
    }
}

dependencies {
    implementation files("libs/mog-android-release-<version>.aar")
}
```

Import the Prefab package from the application's `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.22.1)
project(game LANGUAGES CXX)

find_package(mog REQUIRED CONFIG)

add_library(game SHARED game.cpp)
target_compile_features(game PRIVATE cxx_std_20)
target_link_libraries(game PRIVATE mog::mog)
```

The AAR was built with NDK r27 and `c++_shared`. An Android process must use one
compatible C++ runtime across all of its native libraries. Using NDK r27 and
`-DANDROID_STL=c++_shared` for the application is recommended; Gradle packages
the required `libc++_shared.so`.

Declare normal network access in the application manifest:

```xml
<uses-permission android:name="android.permission.INTERNET" />
```

Then call mog from native code exactly as on other native platforms:

```cpp
#include <mog/mog.hpp>

auto response = mog::get("https://api.example.com/game-state");
if (response) {
    UseState(response->body);
} else {
    ReportNetworkError(response.error().to_string());
}
```

## HTTP, HTTPS, and backend behavior

`Backend::Auto` selects `Backend::Embedded` on Android. The AAR contains mog's
socket-based HTTP/1.1 implementation, mbedTLS, gzip/deflate support, and the
embedded Mozilla CA roots. It does not discover or load an Android system
libcurl, and the `curl`, `native`, `winhttp`, and `web` selectors are
unavailable.

HTTPS works without a Java networking bridge. With verification enabled (the
default), a caller-supplied PEM bundle and the documented CA environment
overrides take precedence; otherwise the embedded Mozilla roots provide the
portable fallback. Android's Network Security Configuration does not configure
mog's raw native socket/mbedTLS transport. Supply `Options::ca_bundle` when an
application needs private roots or a trust policy different from the embedded
bundle.

The Android client supports the native embedded feature set: redirects,
sessions and keep-alive, cookies, streaming responses, multipart bodies,
gzip/deflate, Basic/Bearer/Digest authentication, mTLS, proxies, timeouts, and
response limits. `Response::backend` is `"embedded"`.

`mog::Server` is also included. It can listen on loopback or an app-accessible
interface and supports the same embedded HTTP/S server features as other native
platforms. The application remains responsible for Android lifecycle,
background-execution, routing, and firewall constraints; mog does not create an
Android Service.

## Package contents and limits

The AAR contains:

- `jni/<abi>/libmog.so` and the compatible `libc++_shared.so` runtime;
- `prefab/modules/mog/include/mog/...` public headers, including the generated
  version header; and
- a `mog` Prefab module for `find_package(mog)` and `mog::mog`.

The Android package is the core C++ library. It does not build the mog CLI,
benchmarks, the separate `libmog_c` FFI package, or the optional CLI11,
nlohmann/json, and spdlog integrations. Applications can use their own JSON and
logging dependencies alongside mog's core API.

For repository builds, device tests, CI, and release packaging, see the
[Android maintainer guide](android-maintainers.html).
