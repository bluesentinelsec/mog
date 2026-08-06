---
title: iOS
description: "Use mog from C++ iPhone and iPad applications through the release XCFramework."
---

# iOS

The iOS release is a native C++20 HTTP/S client and server packaged as a static
`mog.xcframework`. It exposes mog's normal C++ API to iPhone and iPad
applications; it does not add a Swift, Objective-C, or Objective-C++ wrapper
API.

iOS 13 or newer is supported. The XCFramework contains:

- an arm64 library for physical iOS devices; and
- a universal arm64/x86_64 library for iOS Simulator.

## Add the XCFramework to an application

Download `mog-ios-xcframework-release-<version>.zip` from the
[GitHub release](https://github.com/bluesentinelsec/mog/releases) and extract
`mog.xcframework`.

For an Xcode project, drag the XCFramework into the application target's
**Frameworks, Libraries, and Embedded Content** section and choose **Do Not
Embed**. It is a static library. Set the target's C++ language dialect to C++20
or newer. The application supplies Apple's libc++ runtime and normal code
signing; the package does not contain or sign an app binary.

In a CMake-generated Xcode project, CMake 3.28 or newer can select the correct
XCFramework slice and add its headers automatically:

```cmake
cmake_minimum_required(VERSION 3.28)
project(game LANGUAGES CXX OBJCXX)

add_executable(game MACOSX_BUNDLE game_main.mm game.cpp)
target_compile_features(game PRIVATE cxx_std_20)
target_link_libraries(game PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/Frameworks/mog.xcframework"
  "-framework Foundation"
)
```

Then call mog from C++ or Objective-C++ exactly as on another native platform:

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

`Backend::Auto` selects `Backend::Native` on iOS. The native backend uses
`NSURLSession`, so HTTPS uses the iOS trust store and normal Apple URL-loading
behavior. `Response::backend` is `"native"`.

An explicit PEM CA bundle, PEM client certificate, or HTTP proxy is not handled
by mog's NSURLSession transport. When `Auto` sees `Options::ca_bundle`,
`Options::client_cert`, or `Options::proxy`, it selects the packaged embedded
mbedTLS backend for that request. Selecting `Backend::Native` explicitly
disables that fallback and rejects the unsupported configuration. The
XCFramework also exposes `Backend::Embedded` explicitly; `curl`, `winhttp`, and
`web` are unavailable on iOS.

Plain HTTP through NSURLSession is governed by the application's App Transport
Security policy. HTTPS needs no exception. If an application intentionally
uses cleartext HTTP, configure the narrowest appropriate ATS exception in its
`Info.plist`; mog does not weaken the application's policy.

The native client supports redirects, sessions, cookies, streaming responses,
response limits, gzip/deflate, and Basic/Bearer/Digest authentication. The
embedded fallback supplies socket-based HTTP/1.1, mbedTLS, the Mozilla CA roots,
mTLS, custom PEM roots, and proxy support under the same public API.

## Embedded server on iOS

`mog::Server` is included in the XCFramework. It can serve HTTP or HTTPS while
the application is active, including self-signed TLS for development and local
game tooling. The application owns iOS lifecycle, background execution, local
network privacy declarations, and interface selection. mog does not keep an app
alive in the background or add entitlements.

Loopback servers do not require public network access. Listening on or
connecting to the local network may require an `NSLocalNetworkUsageDescription`
and is subject to the user's Local Network permission.

## Package contents and limits

Each XCFramework slice contains one self-contained static `libmog` archive and
the public `mog/...` headers, including the generated version header. The
archive folds in the embedded HTTP/TLS implementation, mbedTLS, miniz, and the
Apple NSURLSession transport; callers do not separately build those libraries.

The iOS package is the core C++ library. It does not include the mog CLI,
benchmarks, the separate shared `mog_c` FFI package, or the optional CLI11,
nlohmann/json, and spdlog integrations. Applications can use their own JSON and
logging dependencies alongside mog's core API.

For repository builds, Simulator tests, CI, and release packaging, see the
[iOS maintainer guide](ios-maintainers.html).
