# mog

**mog** is a lightweight, cross-platform HTTP/S client library and CLI for C++.

It prioritizes **easy shipping**: the default backend is a static-link-friendly
embedded HTTP/1.1 stack (sockets + [mbedTLS](https://www.trustedfirmware.org/projects/mbed-tls/)),
so you do not need libcurl or OpenSSL installed to build or run.

Platform-native backends (WinHTTP, libcurl via `dlopen`, NSURLSession) are planned
and selectable by name; today only **`embedded`** is implemented.

```bash
mog get https://example.com
mog post https://httpbin.org/post -d '{"ok":true}' -H 'Content-Type: application/json'
```

[![CI](https://github.com/bluesentinelsec/mog/actions/workflows/ci.yml/badge.svg)](https://github.com/bluesentinelsec/mog/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

---

## Why

Most C++ HTTP options either:

- pull a heavy dependency graph (libcurl + TLS + friends), or
- are incomplete for HTTPS in minimal environments.

**mog** starts from an embedded, always-available client, then will optionally use
OS-native stacks when you ask for them.

---

## Features (v0.1)

| Area | Status |
|------|--------|
| HTTP/1.1 GET/POST/PUT/PATCH/DELETE/HEAD/OPTIONS | yes |
| HTTPS via mbedTLS | yes |
| Redirects | yes |
| Chunked transfer encoding | yes |
| Thread-safe free functions + `Session` | yes |
| requests-like C++ API | yes |
| CLI (`mog`) | yes |
| Backend override via CLI / `MOG_BACKEND` | yes |
| curl / WinHTTP / NSURLSession backends | planned |
| HTTP server | deferred |

---

## Build

Requirements: CMake 3.20+, C++20 compiler, Ninja recommended, network for first
configure (FetchContent: mbedTLS, CLI11, GTest, …).

```bash
git clone https://github.com/bluesentinelsec/mog.git
cd mog
make          # Debug
make test
./mog get https://example.com -v
```

Windows: `build.bat` / `build.bat test`.

---

## CLI

```text
mog get URL [options]
mog post URL [options]
mog [options] URL
```

| Flag | Description |
|------|-------------|
| `-X, --request METHOD` | HTTP method (bare form) |
| `-H, --header "Name: value"` | Request header (repeatable) |
| `-d, --data BODY` | Request body |
| `-o, --output FILE` | Write body to file |
| `--backend NAME` | `auto` \| `embedded` \| `curl` \| `winhttp` \| `native` |
| `--timeout SEC` | Timeout in seconds (default 30) |
| `-k, --insecure` | Disable TLS verification |
| `-v, --verbose` | Progress on stderr |
| `-i, --include` | Include response headers in output |
| `-f, --fail` | Non-zero exit on HTTP 4xx/5xx |
| `-V, --version` | Print version |

### Backend selection

Precedence (highest first):

1. CLI `--backend`
2. Environment variable `MOG_BACKEND`
3. Default: **`embedded`**

```bash
export MOG_BACKEND=embedded
mog get https://example.com --backend embedded
```

---

## Library API (requests-style)

```cpp
#include <mog/mog.hpp>
#include <iostream>

int main() {
    auto r = mog::get("https://example.com");
    if (!r) {
        std::cerr << r.error().to_string() << "\n";
        return 1;
    }
    std::cout << r->status_code << "\n" << r->text();

    mog::Options opt;
    opt.headers["Accept"] = "application/json";
    opt.timeout = std::chrono::seconds(10);
    opt.backend = mog::Backend::Embedded; // optional override
    auto r2 = mog::post("https://httpbin.org/post", opt);

    mog::Session session;
    session.set_header("User-Agent", "my-app/1.0");
    auto r3 = session.get("https://example.com");
}
```

Free functions and `Session` methods are **thread-safe**. `Session` takes a
snapshot of defaults under a mutex per request.

Link against `mog::mog` / `mog::lib` (see generated CMake package config).

---

## Project layout

```text
include/mog/          Public headers
src/http/             Client implementation + embedded backend
src/main.cpp          CLI entrypoint
tests/http/           Unit tests
```

Bootstrapped with [cppboot](https://github.com/bluesentinelsec/cppboot).

---

## License

MIT — see [LICENSE](LICENSE).

mbedTLS is fetched at build time under its own license (Apache-2.0).
