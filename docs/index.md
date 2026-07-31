---
title: Home
description: "mog is a lightweight, static-link-friendly C++ HTTP/S client and CLI with an embedded fallback and OS-native backends."
---

<div class="hero">
  <img class="banner" src="assets/mog-banner.png" alt="mog: a moogle delivering an HTTP/S package">
  <h1>mog</h1>
  <p class="tagline">A lightweight, static-link-friendly HTTP/S client for C++. One binary, encrypted requests, no dependency headaches.</p>
  <div class="badges">
    <span>C++17</span><span>Linux · macOS · Windows</span><span>MIT</span><span>TLS via mbedTLS</span><span>zero required deps</span>
  </div>
  <div class="cta">
    <a class="btn primary" href="#quick-start">Quick start</a>
    <a class="btn ghost" href="cli.html">CLI manual</a>
  </div>
</div>

## Why mog

C++ has no shortage of HTTP libraries, but shipping one usually means dragging in libcurl, OpenSSL, and a chain of transitive dependencies. mog takes the opposite approach. It prefers the HTTP stack your operating system already ships: libcurl on Linux (loaded at runtime), NSURLSession on macOS, and WinHTTP on Windows. When none of those is available, it falls back to a small **embedded** stack (HTTP/1.1 + mbedTLS) that is statically linked and always present.

The result is a client you can **static-link into a single binary** and run anywhere. That includes a `FROM scratch` container with nothing on disk but the executable. HTTPS still verifies there, because the Mozilla CA bundle is compiled in.

- **No dependency headaches.** The embedded backend needs nothing installed to build or run.
- **Native where it counts.** `Auto` uses the OS stack (HTTP/2, system trust) when it is present.
- **Graceful fallback.** When no native library is available, mog transparently uses embedded.
- **Requests-style API** with a **curl-style CLI**.

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

<div class="grid">
  <div class="card"><h3>Static &amp; self-contained</h3><p>Embedded HTTP/1.1 + mbedTLS + bundled Mozilla CAs. Runs on scratch images.</p></div>
  <div class="card"><h3>OS-native backends</h3><p>curl (dlopen), NSURLSession, and WinHTTP. Preferred automatically, at feature parity.</p></div>
  <div class="card"><h3>TLS done right</h3><p>Hybrid trust: system store first, then embedded roots, so HTTPS verifies everywhere.</p></div>
  <div class="card"><h3>Real features</h3><p>Redirects, cookies, gzip, streaming, multipart uploads, Basic/Bearer/Digest, mTLS, proxy.</p></div>
</div>

## Backends at a glance

`Auto` (the default) prefers the platform-native backend and falls back to embedded. It chooses **per request**, so a call never silently loses a feature. Pick one explicitly with `--backend`, `MOG_BACKEND`, or `Options::backend`.

| Backend | Selector | Role |
|---------|----------|------|
| macOS NSURLSession | `native` | Auto default on macOS |
| libcurl (runtime `dlopen`) | `curl` | Auto default on Linux |
| Windows WinHTTP | `winhttp` | Auto default on Windows |
| Embedded (HTTP/1.1 + mbedTLS) | `embedded` | Always-present fallback, and the API you design against |

See the **[Guide](guide.html)** for how selection, TLS trust, and static deployment work, and the **[CLI Manual](cli.html)** for every flag.

## Why "mog"

Short for *moogle*: a small, fluffy courier that delivers packages. In our case, it delivers packets. It asks for no dependencies in return. Kupo.
