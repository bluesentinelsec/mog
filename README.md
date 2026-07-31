<div align="center">

# mog

**A lightweight, static-link-friendly HTTP/S client for C++. One binary, encrypted requests, no dependency headaches.**

[![CI](https://github.com/bluesentinelsec/mog/actions/workflows/ci.yml/badge.svg)](https://github.com/bluesentinelsec/mog/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-informational.svg)](LICENSE)
&nbsp;·&nbsp; **[Documentation](https://bluesentinelsec.github.io/mog/)** ·
[CLI Manual](https://bluesentinelsec.github.io/mog/cli.html) ·
[Guide](https://bluesentinelsec.github.io/mog/guide.html)

</div>

---

<div align="center">
  <img src="docs/assets/mog-banner.png" alt="mog: a moogle delivering an HTTP/S package" width="820">
</div>

C++ has plenty of HTTP libraries, but shipping one usually drags in libcurl,
OpenSSL, and a chain of transitive dependencies. mog does the opposite. It
prefers the HTTP stack your operating system already ships. That means libcurl
on Linux (loaded at runtime), NSURLSession on macOS, and WinHTTP on Windows. When
none of those is available, it falls back to a small embedded stack (HTTP/1.1 +
mbedTLS) that is statically linked and always present.

You can static-link mog into a single binary and run it anywhere. That includes a
`FROM scratch` container with nothing on disk but the executable. HTTPS still
verifies there, because the Mozilla CA bundle is compiled into the binary.

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

- **Static and self-contained.** The embedded backend bundles HTTP/1.1, mbedTLS, and the Mozilla CA roots. It runs on scratch images.
- **OS-native backends.** curl (`dlopen`), NSURLSession, and WinHTTP are preferred automatically, and they are at feature parity with the fallback.
- **Hybrid TLS trust.** The system store is tried first, then the embedded roots. HTTPS verifies everywhere.
- **Requests-style library and a curl-style CLI.**
- Redirects, cookies (domain, path, and Secure), gzip and deflate, streaming download, multipart upload, Basic, Bearer, and Digest auth, mTLS, HTTP proxy, and JSON interop.

## Backends

`Auto` (the default) prefers the platform-native backend and falls back to
embedded. It chooses **per request**, so a call never silently loses a feature.
Select one explicitly with `--backend`, `MOG_BACKEND`, or `Options::backend`.

| Backend | Selector | Role |
|---------|----------|------|
| macOS NSURLSession | `native` | Auto default on macOS |
| libcurl (runtime `dlopen`) | `curl` | Auto default on Linux |
| Windows WinHTTP | `winhttp` | Auto default on Windows |
| Embedded (HTTP/1.1 + mbedTLS) | `embedded` | Always-present fallback, and the API you design against |

## Documentation

- **[Documentation site](https://bluesentinelsec.github.io/mog/)**: the landing page and full docs.
- **[CLI Manual](docs/cli.md)**: every flag, exit codes, environment variables, and write-out tokens.
- **[Guide](docs/guide.md)**: backends, TLS trust, request behavior, and static or scratch deployment.
- **[Library](docs/library.md)**: the C++ API, covering `Options`, `Response`, `Session`, streaming, multipart, and auth.

## Build

```bash
make            # Debug build + tests
make release    # optimized
make test       # unit tests
```

Windows uses `build.bat` or `build.bat test`. To produce a fully static Linux
binary for scratch or minimal images, run
`docker build -f docker/Dockerfile.linux-static -t mog-static .`

## Why "mog"?

Short for *moogle*: a small, fluffy courier that delivers packages. In our case,
it delivers packets. It asks for no dependencies in return. Kupo.

## License

MIT. See [LICENSE](LICENSE).
