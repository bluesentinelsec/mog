# mog

**mog** is a lightweight, cross-platform HTTP/S client library and CLI for C++.

It prioritizes **easy shipping**: the default backend is a static-link-friendly
embedded HTTP/1.1 stack (sockets + [mbedTLS](https://www.trustedfirmware.org/projects/mbed-tls/)),
so you do not need libcurl or OpenSSL installed to build or run.

Platform-native backends (WinHTTP, libcurl via `dlopen`, NSURLSession) are planned
and selectable by name; today only **`embedded`** is implemented — and it is the
API surface you should design against.

```bash
mog get https://example.com
mog post https://httpbin.org/post --json '{"ok":true}'
mog get https://httpbin.org/basic-auth/u/p -u u:p -f
```

[![CI](https://github.com/bluesentinelsec/mog/actions/workflows/ci.yml/badge.svg)](https://github.com/bluesentinelsec/mog/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

---

## Features

| Capability | Library | CLI |
|------------|---------|-----|
| GET/POST/PUT/PATCH/DELETE/HEAD/OPTIONS | yes | yes |
| HTTPS (mbedTLS) | yes | yes |
| Query params | `Options::params` | URL / `-G -d` |
| JSON body | `WithJson` / `Options::json` | `--json` |
| Form body (urlencoded) | `WithForm` / `Options::form` | `-F name=value` |
| Raw body / file body | `Options::body` / `ReadFile` | `-d`, `-d @file` |
| Basic auth | `WithBasicAuth` | `-u user:pass` |
| Bearer token | `WithBearerToken` | `--bearer` |
| Custom headers | `Options::headers` | `-H` |
| Cookies (send + Set-Cookie parse) | yes + Session jar | `-b` |
| Redirects | yes (default on) | `--no-location`, `--max-redirs` |
| Timeouts (I/O + connect) | yes | `--timeout`, `--connect-timeout` |
| TLS verify / CA bundle | yes | `-k`, `--cacert` |
| HTTP proxy (+ HTTPS CONNECT) | `Options::proxy` | `-x` |
| Response size limit | `max_response_bytes` | (library) |
| Thread-safe free functions + Session | yes | n/a |
| Backend override | CLI / env / Options | `--backend`, `MOG_BACKEND` |
| curl / WinHTTP / NSURLSession backends | planned | planned |
| Multipart file upload | not yet | not yet |
| HTTP/2, WebSocket, cookie domain/path | not yet | not yet |
| Content-Encoding gzip | not yet (identity) | not yet |
| HTTP server | deferred | deferred |

---

## Build

```bash
git clone https://github.com/bluesentinelsec/mog.git
cd mog
make && make test
./build/debug/bin/mog get https://example.com -v
```

Windows: `build.bat` / `build.bat test`.

---

## Library API

```cpp
#include <mog/mog.hpp>
#include <iostream>

int main() {
    // One-shot GET
    auto r = mog::get("https://example.com");
    if (!r) {
        std::cerr << r.error().to_string() << "\n";
        return 1;
    }
    std::cout << r->status_code << " " << r->elapsed.count() << "ms\n";
    std::cout << r->text();

    // JSON POST
    mog::Options opt;
    mog::WithJson(opt, R"({"name":"mog"})");
    mog::WithBearerToken(opt, "secret-token");
    opt.timeout = std::chrono::seconds(15);
    auto r2 = mog::post("https://api.example.com/v1/items", opt);

    // Form POST
    auto r3 = mog::post("https://example.com/login",
                        mog::FormOptions({{"user", "a"}, {"pass", "b"}}));

    // Session with cookie jar + defaults
    mog::Session s;
    s.set_base_url("https://api.example.com");
    s.set_header("Accept", "application/json");
    s.set_bearer_token("token");
    auto r4 = s.get("/v1/me");
    // Set-Cookie from r4 is stored; later calls send Cookie automatically.

    if (auto e = r4->raise_for_status(); !e) {
        std::cerr << e.error().to_string() << "\n";
    }
}
```

### `Options` (requests-style)

| Field | Purpose |
|-------|---------|
| `headers` | Extra request headers |
| `body` | Raw body |
| `json` | JSON body (+ `Content-Type: application/json`) |
| `form` | urlencoded form fields |
| `params` | Query string parameters |
| `cookies` | Cookie name → value |
| `auth` | Basic or Bearer (`WithBasicAuth` / `WithBearerToken`) |
| `timeout` | I/O deadline (default 30s) |
| `connect_timeout` | Optional connect-only deadline |
| `verify_tls` | Certificate verification (default true) |
| `ca_bundle` | PEM path (else `SSL_CERT_FILE` / system paths) |
| `allow_redirects` / `max_redirects` | Redirect policy |
| `proxy` | `http://host:port` |
| `max_response_bytes` | Body size cap (default 64 MiB) |
| `backend` | Optional backend override |
| `user_agent` | Default User-Agent if not set |

Body precedence: **`json` > `form` > `body`**.

### `Response`

| Member / method | Purpose |
|-----------------|---------|
| `status_code`, `reason`, `url` | Status and final URL |
| `headers` | Ordered list (duplicates preserved) |
| `header` / `header_all` | Case-insensitive lookup |
| `body` / `text()` / `content()` | Payload |
| `cookies` | Parsed `Set-Cookie` name/value |
| `history` / `history_len` | Redirect chain |
| `elapsed` | Wall time for the exchange |
| `backend` | e.g. `"embedded"` |
| `ok()` / `is_redirect()` / `raise_for_status()` | Status helpers |

### Backend selection

1. `Options::backend` / CLI `--backend`
2. Env `MOG_BACKEND`
3. Default: **`embedded`**

---

## CLI

```text
mog get URL [options]
mog post URL [options]
mog [options] URL
```

| Flag | Description |
|------|-------------|
| `-X METHOD` | HTTP method (bare form) |
| `-H "Name: value"` | Header |
| `-d DATA` | Body (`@file` reads a file) |
| `--json DATA` | JSON body (`@file` ok); implies POST if method is GET |
| `-F name=value` | Form field (urlencoded) |
| `-u user:pass` | Basic auth |
| `--bearer TOKEN` | Bearer auth |
| `-A UA` | User-Agent |
| `-e URL` | Referer |
| `-b "a=1; b=2"` | Cookies |
| `-x http://host:port` | HTTP proxy |
| `--cacert PATH` | CA bundle |
| `--timeout SEC` | I/O timeout |
| `--connect-timeout SEC` | Connect timeout |
| `--max-redirs N` | Max redirects (default 5) |
| `--no-location` | Do not follow redirects |
| `-k` | Insecure TLS |
| `-o FILE` | Write body to file |
| `-D FILE` | Dump response headers to file |
| `-i` | Include headers in body output |
| `-f` | Fail on HTTP 4xx/5xx (exit 22) |
| `-v` | Verbose |
| `-s` / `-S` | Silent / show errors with silent |
| `-G` | With `-d`, append data as query string |
| `-w FORMAT` | `%{http_code}` `%{url_effective}` `%{time_total}` `%{size_download}` `%{num_redirects}` |
| `--backend NAME` | Backend override |
| `-V` | Version |

---

## Project layout

```text
include/mog/     Public headers
src/http/        Client + embedded backend
src/main.cpp     CLI
tests/http/      Unit + local-server integration tests
```

Bootstrapped with [cppboot](https://github.com/bluesentinelsec/cppboot).

---

## License

MIT — see [LICENSE](LICENSE).

mbedTLS is fetched at build time under its own license (Apache-2.0).
