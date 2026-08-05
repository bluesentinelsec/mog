---
title: Web / Emscripten
description: "Use mog as an HTTP and HTTPS client from browser WebAssembly."
---

# Web / Emscripten

The Emscripten build is a browser HTTP and HTTPS **client**. It keeps mog's
synchronous C++ request API while using the browser Fetch API for networking.
It does not emulate sockets and it cannot run `mog::Server`.

```cpp
#include <mog/mog.hpp>

auto response = mog::get("https://api.example.com/game-state");
if (!response) {
    ShowNetworkError(response.error().to_string());
} else {
    UseState(response->body);
}
```

`Backend::Auto` selects `Backend::Web` in an Emscripten build. You normally do
not need to select it explicitly. The embedded, curl, WinHTTP, and NSURLSession
backends are not included in that build.

## HTTPS, CORS, and browser security

HTTPS works through Fetch and the browser's TLS implementation. The browser
chooses its trusted roots, validates the server certificate, negotiates TLS,
and reports an untrusted or otherwise invalid connection as a Fetch failure.
WebAssembly code cannot disable verification, install a per-request CA bundle,
or provide a client certificate.

Normal browser security rules still apply:

- A page served over HTTPS generally cannot fetch an HTTP resource because of
  mixed-content policy. Use an HTTPS API endpoint.
- A same-origin request works without CORS configuration. For a cross-origin
  request, the API server must allow the page's origin, method, and request
  headers. Custom headers and JSON requests may cause a CORS preflight.
- Cross-origin response headers are limited to the CORS-safelisted headers plus
  fields named by the server's `Access-Control-Expose-Headers` response header.
- A CORS, mixed-content, DNS, connection, or TLS rejection is intentionally
  opaque to JavaScript. mog reports the Fetch failure as `ConnectFailed`, but
  the browser developer console usually contains the more specific reason.

The web backend uses Fetch's `credentials: "same-origin"` policy. The browser
may send its cookies to the page's own origin, but it does not send cookies to a
different origin. JavaScript cannot set a `Cookie` header or read `Set-Cookie`.

## Supported request behavior

The web backend supports:

- `GET`, `POST`, `PUT`, `PATCH`, `DELETE`, `HEAD`, and `OPTIONS`;
- query parameters and buffered raw, JSON, form, and multipart request bodies;
- caller headers that browsers permit, including Basic and Bearer
  `Authorization` headers when allowed by CORS;
- buffered binary or text response bodies and browser-exposed response headers;
- `Options::timeout` and `Options::max_response_bytes`; and
- browser-managed redirects, content decoding, connection reuse, DNS, and TLS.

`Response::backend` is `"web"`, `Response::url` is the final URL Fetch exposes,
and `Response::body` contains the complete response body. Responses are
buffered in browser and Wasm memory, so use a suitable `max_response_bytes` and
avoid this backend for unbounded downloads.

The following options return `InvalidArgument` instead of being silently
ignored:

| Option or behavior | Browser constraint |
|---|---|
| `verify_tls = false` | TLS verification is mandatory and browser-controlled. |
| `ca_bundle`, `client_cert`, or `client_key` | Fetch cannot customize trust roots or present a per-request client certificate. |
| `proxy` | JavaScript cannot choose a per-request network proxy. |
| `response_writer` | Fetch responses are currently buffered by the mog backend. |
| `decompress = false` | Content decoding is browser-controlled. |
| Digest authentication | The web backend does not perform the Digest challenge flow. |
| `Options::cookies` | The browser owns the `Cookie` header. |
| A body on `GET` or `HEAD` | Fetch rejects these request bodies. |

Other native controls do not have a direct Fetch equivalent.
`connect_timeout` cannot isolate the connect phase from the overall timeout;
`keep_alive` cannot control the browser's connection pool; and mog cannot
enforce `max_redirects` precisely or expose the redirect chain. With
`allow_redirects = false`, a browser cross-origin redirect is opaque, so mog
returns `ProtocolError` rather than a partially observable response.

`Session` can still provide a base URL and default request options, but the
browser—not the Session cookie jar or connection pool—owns cookies and
connection reuse.

## Synchronous API and Asyncify

Fetch is asynchronous, so mog uses Emscripten Asyncify to suspend the Wasm call
stack while the browser completes a request and then resume the C++ call. The
exported `mog::lib` CMake target supplies `-sASYNCIFY=1` transitively to the
final application link.

Asyncify adds code-size and runtime overhead. Avoid blocking a request from a
per-frame hot path. The backend permits one in-flight synchronous request per
Wasm module; if browser re-entry tries to start another request while the first
is suspended, that request fails with `InvalidArgument`.

## Consume a release package

Download and extract `mog-web-wasm32-release-<version>.zip` from the
[GitHub release](https://github.com/bluesentinelsec/mog/releases). Configure the
application with the Emscripten toolchain and point CMake at the extracted
package:

```bash
emcmake cmake -S . -B build/web -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/absolute/path/to/mog-web-wasm32-release-<version>
cmake --build build/web --parallel
```

Link the imported target from the application's `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(game LANGUAGES CXX)

find_package(mog CONFIG REQUIRED)

add_executable(game src/main.cpp)
target_compile_features(game PRIVATE cxx_std_20)
target_link_libraries(game PRIVATE mog::lib)
set_target_properties(game PROPERTIES SUFFIX ".html")
```

The package contains `libmog.a`, public headers, and CMake package files. It
does not contain a standalone `.wasm`, because the application's final link
creates the JavaScript and Wasm modules. Using the SDK version recorded in the
package's `EMSCRIPTEN_VERSION` file is recommended.

If linking `libmog.a` without its exported CMake target, the application is
responsible for adding `-sASYNCIFY=1` to its final Emscripten link.

## Consume from source

An application may also add mog as a subdirectory or with `FetchContent`. The
entire application must be configured through `emcmake`; a native build cannot
link an Emscripten archive.

```cmake
set(MOG_BUILD_APP OFF CACHE BOOL "" FORCE)
set(MOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MOG_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(MOG_BUILD_C_SHARED OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/mog)

target_link_libraries(game PRIVATE mog::lib)
```

The CLI, benchmarks, shared C FFI library, runtime shared-library loading, and
HTTP/S server are not part of an Emscripten build. Host game services in a
native process or dedicated service and expose an HTTPS endpoint with the
appropriate CORS policy.

For repository builds, browser tests, CI, and release packaging, see the
[Web maintainer guide](web-maintainers.html).
