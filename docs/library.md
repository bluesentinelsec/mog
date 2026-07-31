---
title: Library
description: "Using mog as a C++ library: requests-style free functions, Session, Options, Response, JSON, streaming, multipart, and auth."
---

# Library

mog is a C++17 library with a requests-style API. Include the umbrella header:

```cpp
#include <mog/mog.hpp>
```

Every call returns a `Result<T>` (an expected-like type): truthy on success,
otherwise `.error()` holds a stable `ErrorCode` + message. No exceptions.

## Free functions

```cpp
auto r = mog::get("https://example.com");
if (!r) { /* r.error().to_string() */ }
else    { /* r->status_code, r->headers, r->text() */ }

mog::post(url, opt);  mog::put(url, opt);  mog::patch(url, opt);
mog::del(url, opt);   mog::head(url, opt); mog::request(mog::Method::Get, url, opt);
```

## Options

```cpp
mog::Options opt;
opt.headers["X-Api-Key"] = "…";
opt.timeout = std::chrono::seconds(10);
opt.backend = mog::Backend::Curl;      // force a backend (default: Auto)
```

| Field | Purpose |
|-------|---------|
| `headers` | Extra request headers |
| `body` / `json` / `form` / `multipart` | Request body (precedence: multipart > json > form > body) |
| `params` | Query-string parameters |
| `cookies` | Per-request cookies |
| `auth` | Basic / Bearer / Digest (`WithBasicAuth` / `WithBearerToken` / `WithDigestAuth`) |
| `timeout` / `connect_timeout` | Deadlines |
| `verify_tls` / `ca_bundle` | TLS verification and custom CA |
| `client_cert` / `client_key` / `client_key_password` | mTLS (`WithClientCert`) |
| `allow_redirects` / `max_redirects` | Redirect policy |
| `proxy` | `http://host:port` |
| `max_response_bytes` | Body size cap |
| `decompress` | Decode gzip/deflate (default true) |
| `response_writer` | Stream the body to a sink (see below) |
| `backend` | Backend override |

## Response

```cpp
r->status_code;      // int
r->headers;          // std::vector<Header> (name/value, order preserved)
r->text();           // const std::string& (body)
r->header("Content-Type");
r->downloaded_bytes; // bytes received
r->backend;          // "embedded" | "curl" | "native" | "winhttp"
r->ok();             // 2xx/3xx
r->raise_for_status();
```

## Session

A reusable client with default options, a cookie jar, and connection reuse:

```cpp
mog::Session s;
s.set_bearer_token("token");
s.set_base_url("https://api.example.com");
auto me = s.get("/me");     // cookies + keep-alive carried across calls
```

## Streaming

Deliver the body incrementally instead of buffering it:

```cpp
mog::Options opt;
auto w = mog::FileWriter("big.iso");            // or a custom sink
if (!w) { /* FileError */ }
opt.response_writer = std::move(*w);
auto r = mog::get("https://example.com/big.iso", opt);   // r->body stays empty
```

## Multipart uploads

```cpp
mog::Options opt;
mog::AddFormField(opt, "user", "alice");
mog::AddFormFile(opt, "avatar", "me.png", png_bytes, "image/png");
auto from_disk = mog::AddFormFileFromPath(opt, "report", "report.pdf");
auto r = mog::post("https://example.com/upload", opt);
```

## JSON (nlohmann/json)

When built with `MOG_WITH_JSON` (default for top-level builds), `#include <mog/mog.hpp>`
pulls in `mog/json.hpp` and `WithJson` accepts an `nlohmann::json`:

```cpp
nlohmann::json payload = {{"name", "mog"}};
auto r = mog::post(url, mog::JsonOptions(payload.dump()));
```

## Building / consuming

```bash
make            # Debug build + tests
make release
```

The library target is `mog_lib` (static). See the
[README](https://github.com/bluesentinelsec/mog) for CMake / FetchContent usage
and the [Guide](guide.html) for backend and TLS behavior.
