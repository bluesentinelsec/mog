---
title: Home
description: "mog is a lightweight, static-link-friendly C++ HTTP/S client and CLI with an embedded fallback and OS-native backends."
---

<div class="hero">
  <img class="banner" src="assets/mog-banner.png" alt="mog: a moogle delivering an HTTP/S package">
  <h1>mog</h1>
  <p class="tagline">A lightweight, static-link-friendly HTTP/S client for C++. One binary, encrypted requests, no dependency headaches.</p>
  <div class="badges">
    <span class="badge rounded-pill">C++20</span><span class="badge rounded-pill">Linux · macOS · Windows · Android · WebAssembly</span><span class="badge rounded-pill">MIT</span><span class="badge rounded-pill">HTTP/S</span><span class="badge rounded-pill">zero required deps</span>
  </div>
  <div class="d-flex flex-column flex-sm-row gap-2 justify-content-center">
    <a class="btn btn-primary rounded-pill px-4" href="#quick-start">Quick start</a>
    <a class="btn btn-outline-primary rounded-pill px-4" href="cli.html">CLI manual</a>
  </div>
</div>

## Why mog

C++ has no shortage of HTTP libraries, but shipping one usually means dragging in libcurl, OpenSSL, and a chain of transitive dependencies. mog takes the opposite approach. On native platforms it prefers the HTTP stack the operating system already ships: libcurl on Linux (loaded at runtime), NSURLSession on macOS, and WinHTTP on Windows. When none of those is available, it falls back to a small **embedded** stack (HTTP/1.1 + mbedTLS). Browser WebAssembly builds use Fetch and the browser's TLS implementation.

The result is a client you can **link into a native, Android NDK, or WebAssembly application**. A native binary can run in a `FROM scratch` container with nothing else on disk; HTTPS still verifies there because the Mozilla CA bundle is compiled in. Android releases are Prefab AARs with the embedded stack, and browser HTTPS uses browser trust subject to CORS and mixed-content policy.

- **No dependency headaches.** The embedded backend needs nothing installed to build or run.
- **Native where it counts.** Native `Auto` uses the OS stack (HTTP/2, system trust) when it is present.
- **Graceful native fallback.** When no native library is available, mog transparently uses embedded.
- **Requests-style API**, plus a native **curl-style CLI** and **C API** for C and FFI runtimes like Python ctypes.
- **Browser WebAssembly.** Emscripten builds provide an HTTP/S client through Fetch with the same synchronous C++ request API.
- **Android NDK.** A multi-ABI Prefab AAR exposes the same C++ client and server APIs to Gradle/CMake applications.
- **A native embedded HTTP/S server** with routes, static file serving, and TLS, via `mog serve` and `mog::Server`.

## Quick start

**CLI**

```bash
git clone https://github.com/bluesentinelsec/mog.git
cd mog && make
./build/debug/bin/mog get https://example.com -i
```

```bash
mog post https://api.example.com/things --json '{"name":"mog"}' -w '%{http_code}\n'
mog get https://example.com -o page.html            # streamed to disk
mog get https://api.example.com -u user:pass --digest
```

**Library**

```cpp
#include <mog/mog.hpp>

auto r = mog::get("https://example.com");
if (r) {
    // r->status_code, r->headers, r->text()
}
```

<div class="row row-cols-1 row-cols-md-2 g-4 feature-grid">
  <div class="col"><div class="card h-100"><div class="card-body"><h3 class="card-title">Static &amp; self-contained</h3><p class="card-text">Embedded HTTP/1.1 + mbedTLS + bundled Mozilla CAs. Runs on scratch images.</p></div></div></div>
  <div class="col"><div class="card h-100"><div class="card-body"><h3 class="card-title">OS-native backends</h3><p class="card-text">curl (dlopen), NSURLSession, and WinHTTP. Preferred automatically, at feature parity.</p></div></div></div>
  <div class="col"><div class="card h-100"><div class="card-body"><h3 class="card-title">TLS done right</h3><p class="card-text">Native hybrid trust uses system then embedded roots; browser builds use browser TLS.</p></div></div></div>
  <div class="col"><div class="card h-100"><div class="card-body"><h3 class="card-title">Native controls</h3><p class="card-text">Redirects, cookies, gzip, streaming, multipart uploads, Basic/Bearer/Digest, mTLS, proxy.</p></div></div></div>
  <div class="col"><div class="card h-100"><div class="card-body"><h3 class="card-title">Browser WebAssembly</h3><p class="card-text">HTTP/S client via browser Fetch, with CORS and browser-managed TLS.</p></div></div></div>
  <div class="col"><div class="card h-100"><div class="card-body"><h3 class="card-title">Android NDK</h3><p class="card-text">Prefab AAR for ARM and x86_64, with embedded HTTP/S and emulator-tested packaging.</p></div></div></div>
  <div class="col"><div class="card h-100"><div class="card-body"><h3 class="card-title">Embedded server</h3><p class="card-text">Native HTTP/1.1 server with routes, static files, and TLS. <code>mog serve</code> or <code>mog::Server</code>.</p></div></div></div>
</div>

## Backends at a glance

On desktop native platforms, `Auto` (the default) prefers the platform-native backend and falls back to embedded. It chooses **per request**, so a native call never silently loses a feature. Android `Auto` selects embedded and Emscripten `Auto` selects browser Fetch. Pick one explicitly with `--backend`, `MOG_BACKEND`, or `Options::backend`.

| Backend | Selector | Role |
|---------|----------|------|
| macOS NSURLSession | `native` | Auto default on macOS |
| libcurl (runtime `dlopen`) | `curl` | Auto default on Linux |
| Windows WinHTTP | `winhttp` | Auto default on Windows |
| Android embedded | `embedded` | Auto default in Android NDK applications |
| Browser Fetch (Emscripten) | `web` | Auto default in browser WebAssembly |
| Embedded (HTTP/1.1 + mbedTLS) | `embedded` | Always-present native fallback, and the API you design against |

See the **[Guide](guide.html)** for backend selection and native deployment, the **[Android guide](android.html)** for Prefab integration, the **[Web / Emscripten guide](web.html)** for browser behavior, and the **[CLI Manual](cli.html)** for every flag.

## Why "mog"

Short for *moogle*: a small, fluffy courier that delivers packages. In our case, it delivers packets. It asks for no dependencies in return. Kupo.
