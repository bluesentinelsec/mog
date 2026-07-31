<div align="center">

<img src="docs/assets/mog-banner.png" alt="mog — a moogle delivering an HTTP/S package" width="820">

# mog

**A lightweight, static-link-friendly HTTP/S client for C++ — one binary, encrypted requests, no dependency headaches.**

[![CI](https://github.com/bluesentinelsec/mog/actions/workflows/ci.yml/badge.svg)](https://github.com/bluesentinelsec/mog/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-informational.svg)](LICENSE)
&nbsp;·&nbsp; **[Documentation](https://bluesentinelsec.github.io/mog/)** ·
[CLI Manual](https://bluesentinelsec.github.io/mog/cli.html) ·
[Guide](https://bluesentinelsec.github.io/mog/guide.html)

</div>

---

C++ has plenty of HTTP libraries, but shipping one usually drags in libcurl,
OpenSSL, and a chain of transitive dependencies. **mog does the opposite:** it
prefers the HTTP stack your OS already ships — libcurl on Linux (loaded at
runtime), NSURLSession on macOS, WinHTTP on Windows — and falls back to a small
**embedded** stack (HTTP/1.1 + mbedTLS) that is statically linked and always
available.

You can **static-link mog into a single binary** and run it anywhere — even a
`FROM scratch` container with nothing on disk but the executable — and still make
verified HTTPS calls, because the Mozilla CA bundle is compiled in.

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

## Features

- **Static & self-contained** — embedded HTTP/1.1 + mbedTLS + bundled Mozilla CAs; runs on scratch images.
- **OS-native backends** — curl (`dlopen`), NSURLSession, WinHTTP; preferred automatically and at feature parity.
- **Hybrid TLS trust** — system store → embedded roots, so HTTPS verifies everywhere.
- **Requests-style library** + **curl-style CLI**.
- Redirects, cookies (domain/path/Secure), gzip/deflate, streaming download, multipart upload, Basic/Bearer/Digest auth, mTLS, HTTP proxy, JSON interop.

## Backends

`Auto` (the default) prefers the platform-native backend and falls back to
embedded, choosing **per request** so a call never silently loses a feature.
Select one explicitly with `--backend`, `MOG_BACKEND`, or `Options::backend`.

| Backend | Selector | Role |
|---------|----------|------|
| macOS NSURLSession | `native` | Auto default on macOS |
| libcurl (runtime `dlopen`) | `curl` | Auto default on Linux |
| Windows WinHTTP | `winhttp` | Auto default on Windows |
| Embedded (HTTP/1.1 + mbedTLS) | `embedded` | Always-present fallback; the API you design against |

## Documentation

- **[Documentation site](https://bluesentinelsec.github.io/mog/)** — landing + full docs
- **[CLI Manual](docs/cli.md)** — every flag, exit codes, env vars, write-out tokens
- **[Guide](docs/guide.md)** — backends, TLS trust, request behavior, static/scratch deployment
- **[Library](docs/library.md)** — the C++ API (`Options`, `Response`, `Session`, streaming, multipart, auth)

## Build

```bash
make            # Debug build + tests
make release    # optimized
make test       # unit tests
```

Windows: `build.bat` / `build.bat test`. A fully static Linux binary for
scratch/minimal images: `docker build -f docker/Dockerfile.linux-static -t mog-static .`

## License

MIT — see [LICENSE](LICENSE).
