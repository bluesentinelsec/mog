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
| JSON body (string) | `WithJson` / `Options::json` | `--json` |
| JSON body (nlohmann) | `WithJson(opt, json)` / `post_json` | via `--json` text |
| Parse JSON response | `ParseJson(response)` | n/a |
| Form body (urlencoded) | `WithForm` / `Options::form` | `-d 'a=1&b=2'` |
| Multipart/form-data + file upload | `AddFormField` / `AddFormFile` | `-F name=value`, `-F f=@file` |
| Raw body / file body | `Options::body` / `ReadFile` | `-d`, `-d @file` |
| Basic auth | `WithBasicAuth` | `-u user:pass` |
| Bearer token | `WithBearerToken` | `--bearer` |
| Custom headers | `Options::headers` | `-H` |
| Cookies (send + Set-Cookie parse) | yes + Session jar | `-b` |
| Redirects | yes (**default on**) | `--no-location` to disable, `--max-redirs` |
| Keep-alive / connection reuse | Session default on | (library `Options::keep_alive`) |
| Timeouts (I/O + connect) | yes | `--timeout`, `--connect-timeout` |
| TLS verify / CA trust | hybrid (CLI/env → system → embedded Mozilla) | `-k`, `--cacert` |
| Runtime shared libraries | `mog::SharedLibrary` (dlopen/LoadLibrary) | n/a |
| HTTP proxy (+ HTTPS CONNECT) | `Options::proxy` | `-x` |
| Response size limit | `max_response_bytes` (decoded when decompressing) | (library) |
| Disable decompress | `Options::decompress = false` | `--no-decompress` |
| Thread-safe free functions + Session | yes | n/a |
| Backend override | CLI / env / Options | `--backend`, `MOG_BACKEND` |
| curl / WinHTTP / NSURLSession backends | planned | planned |
| Session cookie jar (domain/path/Secure) | yes | `-b` (per-request) |
| HTTP/2, WebSocket | not yet | not yet |
| Content-Encoding gzip/deflate | yes (miniz, static) | `--no-decompress` to disable |
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

    // JSON POST — string form always works
    mog::Options opt;
    mog::WithJson(opt, R"({"name":"mog"})");
    mog::WithBearerToken(opt, "secret-token");
    opt.timeout = std::chrono::seconds(15);
    auto r2 = mog::post("https://api.example.com/v1/items", opt);

    // JSON POST — nlohmann/json (cppboot default; MOG_WITH_JSON=ON)
    nlohmann::json payload = {{"name", "mog"}, {"n", 1}};
    auto r2b = mog::post_json("https://api.example.com/v1/items", payload);
    if (r2b) {
        if (auto doc = mog::ParseJson(*r2b)) {
            std::cout << (*doc).dump(2) << "\n";
        }
    }

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
| `multipart` | multipart/form-data parts (`AddFormField` / `AddFormFile`) |
| `params` | Query string parameters |
| `cookies` | Cookie name → value |
| `auth` | Basic or Bearer (`WithBasicAuth` / `WithBearerToken`) |
| `timeout` | I/O deadline (default 30s) |
| `connect_timeout` | Optional connect-only deadline |
| `verify_tls` | Certificate verification (default true) |
| `ca_bundle` | PEM path (highest trust precedence; else env/system/embedded) |
| `allow_redirects` / `max_redirects` | Follow 3xx by default; set false / CLI `--no-location` to disable |
| `keep_alive` | Prefer `Connection: keep-alive` (default true); Session also pools by origin |
| `proxy` | `http://host:port` |
| `max_response_bytes` | Body size cap (default 64 MiB; decoded size when decompressing) |
| `decompress` | Decode Content-Encoding gzip/deflate (default true) |
| `response_writer` | Stream body to a sink instead of buffering (see below) |
| `backend` | Optional backend override |
| `user_agent` | Default User-Agent if not set |

Body precedence: **`multipart` > `json` > `form` > `body`**.

### Multipart / file uploads

Build `multipart/form-data` requests with `Options::multipart` (or the helpers).
When any part is present, mog sets `Content-Type: multipart/form-data` with a
generated boundary and takes precedence over `json` / `form` / `body`.

```cpp
mog::Options opt;
mog::AddFormField(opt, "user", "alice");
mog::AddFormFile(opt, "avatar", "me.png", png_bytes, "image/png"); // from memory
auto from_disk = mog::AddFormFileFromPath(opt, "report", "report.pdf"); // reads file
if (!from_disk) { /* FileError */ }
auto r = mog::post("https://example.com/upload", opt);
```

File parts always get a `Content-Type` (guessed from the filename when unset);
the whole body is built in memory (streaming uploads are a non-goal).

> **CLI breaking change:** `-F` now builds `multipart/form-data` (curl-compatible)
> instead of urlencoded fields. `-F name=value` is a text field, `-F name=@path`
> uploads a file (with optional `;type=` / `;filename=`), and `-F name=<path`
> reads a field value from a file. For an urlencoded body use `-d 'a=1&b=2'`.

### Streaming downloads

For large responses, set `Options::response_writer` to deliver the body
incrementally instead of buffering it into `Response::body` (which then stays
empty). `Response::downloaded_bytes` reports how many bytes were streamed.
Memory stays flat regardless of body size, and `max_response_bytes` is still
enforced (0 = unlimited). Works with both `Content-Length` and chunked bodies;
only the final response streams (redirect bodies are skipped).

```cpp
// To a file (helper opens/truncates it and returns a writer):
auto writer = mog::FileWriter("big.iso");
if (!writer) { /* FileError */ }
mog::Options opts;
opts.response_writer = std::move(*writer);
auto r = mog::get("https://example.com/big.iso", opts);   // r->body is empty

// Or to any sink via a callback (return an Error to abort):
mog::Options o;
o.response_writer = [&](std::string_view chunk) -> mog::Result<void> {
    hasher.update(chunk);
    return mog::Result<void>::Ok();
};
```

Streaming delivers the **exact wire bytes**: while a writer is attached mog does
not advertise `Accept-Encoding` and does not decode `Content-Encoding`, so
`decompress` has no effect (you get an identity body by default, like `curl -o`).

### Session cookie jar

`Session` keeps a cookie jar that stores `Set-Cookie` responses and replays them
on later requests, scoped by **domain**, **path**, and the **Secure** flag —
enough for typical login/session APIs.

- **Domain** — with no `Domain` attribute the cookie is *host-only* (exact host
  match). With `Domain=example.com` it also matches subdomains. A `Domain` the
  request host doesn't belong to is rejected (no cross-site setting).
- **Path** — defaults to the request's directory (RFC 6265 default-path); a
  cookie is sent only when the request path is within its path. On a name clash,
  the most specific (longest) path wins.
- **Secure** — `Secure` cookies are sent only over HTTPS. `HttpOnly` is stored
  and sent (it only restricts scripting, which doesn't apply to a client).
- `set_cookie(name, value)` adds a manual cookie that matches any host at `/`.

```cpp
mog::Session s;
s.get("https://api.example.com/login");   // stores Set-Cookie
s.get("https://api.example.com/me");       // matching cookies replayed automatically
```

Intentional non-goals (kept simple on purpose): full RFC 6265 semantics,
`SameSite`, public-suffix-list validation, expiry/`Max-Age` eviction (cookies are
session-lifetime), and capturing `Set-Cookie` emitted on intermediate redirect
hops (only the final response is stored). For one-shot cookies on the free
functions, set `Options::cookies` (sent as-is, no jar).

### nlohmann/json (cppboot)

cppboot projects ship **nlohmann/json** via FetchContent (`MOG_WITH_JSON`, default
ON for top-level builds). When enabled, mog defines `MOG_HAS_JSON=1` and
`#include <mog/mog.hpp>` pulls in `mog/json.hpp`:

```cpp
#include <mog/mog.hpp>
#include <nlohmann/json.hpp>  // also available through mog/json.hpp

nlohmann::json req = {{"user", "a"}, {"ok", true}};
auto res = mog::post_json("https://httpbin.org/post", req);
if (!res) { /* transport error */ }
auto doc = mog::ParseJson(*res);  // Result<nlohmann::json>
```

| API | Role |
|-----|------|
| `WithJson(opt, nlohmann::json)` | Serialize into `Options::json` |
| `JsonOptions(nlohmann::json)` | Build Options with JSON body |
| `post_json` / `put_json` / `patch_json` | One-shot JSON requests |
| `ParseJson(Response)` / `ParseJson(string_view)` | Parse body → `nlohmann::json` |

String overloads remain for CLI and callers that already have serialized JSON.
Disable with `-DMOG_WITH_JSON=OFF` if you embed mog without JSON.

### Logging (spdlog)

cppboot ships **spdlog** (`MOG_WITH_SPDLOG`, default ON; required for the CLI).
Library and CLI share one process-wide logger.

```cpp
#include <mog/mog.hpp>

// Same default stderr-colored logger the CLI uses:
mog::UseDefaultLogger(mog::LogLevel::Debug);

// Or inject your own spdlog logger:
// mog::SetLogger(my_spdlog_logger);

auto r = mog::get("https://example.com");
// → info/debug lines for request, connect, TLS, redirects, response summary
```

| API | Role |
|-----|------|
| `UseDefaultLogger(level)` | Install mog’s stderr color logger |
| `MakeDefaultLogger(level)` | Create without installing |
| `SetLogger(ptr)` | Inject a custom `std::shared_ptr<spdlog::logger>` |
| `GetLogger()` | Active logger (lazy-creates default at Warn) |
| `SetLogLevel` / `GetLogLevel` | Change level on the active logger |
| `MOG_LOG_DEBUG` / `INFO` / … | Macros used inside the library |

CLI flags: `-v` (debug), `-s` (off), `--log-level trace|debug|info|…`.

CLI argument mapping is unit-tested in `tests/cli/` (every flag → `Options` /
method / log level, plus CLI11 parse of subcommands and repeated `-H`/`-F`).

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
| `-F name=value` | multipart part; `name=@file[;type=..;filename=..]` uploads a file, `name=<file` reads a field value from a file |
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
| `-o FILE` | Write body to file (streamed to disk; identity encoding, like `curl -o`) |
| `-D FILE` | Dump response headers to file |
| `-i` | Include headers in body output |
| `-f` | Fail on HTTP 4xx/5xx (exit 22) |
| `-v` | Debug logging (spdlog) |
| `--log-level LEVEL` | Explicit log level (overrides `-v`/`-s`) |
| `-s` / `-S` | Silent logs / show errors with silent |
| `-G` | With `-d`, append data as query string |
| `-w FORMAT` | `%{http_code}` `%{url_effective}` `%{time_total}` `%{size_download}` `%{num_redirects}` |
| `--backend NAME` | Backend override |
| `-V` | Version |

---

## TLS trust (hybrid)

When HTTPS verification is enabled (`verify_tls` / default), mog resolves CA roots
in this order (first successful source wins):

1. **CLI / Options** — `--cacert` / `Options::ca_bundle`
2. **Environment** — `MOG_CA_BUNDLE`, then `SSL_CERT_FILE`, `REQUESTS_CA_BUNDLE`,
   `CURL_CA_BUNDLE`, then `SSL_CERT_DIR` (colon-separated PEM directories)
3. **System** — common OS PEM paths; on Windows, the CryptoAPI `ROOT`/`CA` stores
   via **runtime** `LoadLibrary("crypt32.dll")` (not a static link)
4. **Embedded** — Mozilla CA roots shipped in the binary (`data/cacert.pem`,
   same export as [curl’s cacert.pem](https://curl.se/ca/cacert.pem))
5. **Fail loud** — error text lists how to supply a bundle

Minimal containers (`scratch`, distroless without `ca-certificates`) still verify
public HTTPS via the embedded bundle. Set `MOG_NO_EMBEDDED_CA=1` to forbid that
fallback. A daily GitHub Action refreshes the bundle and opens a PR when Mozilla’s
export changes.

Resource policy used elsewhere in mog:

> **CLI or ENV override → system resources → static/embedded → fail loud**  
> Optional platform libraries are loaded at runtime (`mog::SharedLibrary` /
> `dlopen` / `LoadLibrary`), not hard-linked.

---

## Project layout

```text
include/mog/              Public API (http, session, options, dynload, cli, …)
src/main.cpp              Thin shell → mog::cli::RunArgv only
src/cli/                  parse | prepare | run | output (SRP)
src/dynload/              SharedLibrary implementation
src/http/                 HTTP API + transport registry
src/http/detail/          Embedded stack, TLS, CA store, URL, content encoding
data/cacert.pem           Mozilla CA bundle source (regenerate embed via script)
tests/…                   Unit tests by component
```

- **Main** has no domain logic (library-first, SOLID).
- **New transports:** implement `detail::Transport`, call `RegisterTransport`.
- **Platform APIs:** resolve via `mog::SharedLibrary` at runtime.
- **CLI tests** cover flags without spawning a process.
- **Embedded contract:** `tests/http/conformance_test.cpp` (local server only; no
  public network). Platform backends should match this suite under the same API.

---

## Embedded HTTP contract

The default **embedded** backend is the behavioral baseline. Conformance tests
(`EmbeddedConformance.*` in `ctest`) cover, without leaving the machine:

| Area | Locked behavior |
|------|-----------------|
| Status | 200, 204, 4xx/5xx + `raise_for_status` |
| Bodies | `Content-Length`, chunked TE, empty body, HEAD (no body) |
| Redirects | **Follow by default** (301–308); POST→GET on 301/302/303; preserve POST on 307/308; max redirects; `allow_redirects=false` / `--no-location` to not follow |
| Keep-alive | Session reuses TCP/TLS to same origin when server allows; free functions are connection-per-request |
| Failures | connect refused, `max_response_bytes` (CL + chunked) |
| Auth | Basic and Bearer `Authorization` on the wire |
| Encoding | gzip decode on by default; raw when `decompress=false` |
| Backend | `Response::backend == "embedded"` |

Shared harness: `tests/http/test_support/local_http_server.hpp`.

Bootstrapped with [cppboot](https://github.com/bluesentinelsec/cppboot).

---

## License

MIT — see [LICENSE](LICENSE).

**FetchContent (static) dependencies of note:** mbedTLS (Apache-2.0) for TLS;
[miniz](https://github.com/richgel999/miniz) (MIT) for gzip/deflate Content-Encoding.
**Embedded data:** Mozilla CA roots via curl’s `cacert.pem` export (see
`data/cacert.pem`). Optional OS crypto libraries (e.g. Windows `crypt32.dll`) are
loaded only at runtime when present.
