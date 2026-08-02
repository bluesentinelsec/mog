---
title: C API
description: "Calling mog from C and from FFI runtimes like Python ctypes through the mog_c shared library."
---

# C API

mog ships a small C binding so it can be called from plain C and from foreign
function interface runtimes such as Python ctypes, LuaJIT FFI, and Ruby Fiddle.
The binding lives in a single header, `mog/mog_c.h`, and is provided as a
self-contained shared library named `mog_c`. The C++ core, mbedTLS, miniz, and
the platform HTTP backends are linked into that library, so it has no external
runtime dependencies.

The C API covers both the **client** (one-shot requests with the full set of
common options) and the **server** (see "Running a server" below). On the client
side, sessions with a persistent cookie jar, multipart uploads, and streaming
callbacks are planned for a later release.

## Design

The binding uses opaque handles and borrowed strings, so there are no structs to
lay out and exactly two things to free.

- A **request** handle comes from `mog_request_new()` and is released with `mog_request_free()`.
- A **response** handle comes from `mog_perform()` (or `mog_get()` / `mog_post()`) and is released with `mog_response_free()`.

Every `const char *` and byte buffer returned by an accessor is borrowed from its
handle and stays valid until that handle is freed. You never free an individual
string. Copy anything that must outlive the handle. Strings are UTF-8 and
NUL-terminated. Because a response body can contain embedded NUL bytes, read it
with `mog_response_body()` and its length out-parameter.

Errors never cross the boundary as C++ exceptions. A failed transfer still
returns a non-NULL response handle whose `mog_response_ok()` is `0`. Inspect
`mog_response_error_code()` and `mog_response_error_message()` for the reason.

## C example

```c
#include <mog/mog_c.h>
#include <stdio.h>

int main(void)
{
    mog_request *req = mog_request_new("GET", "https://example.com");
    mog_request_set_header(req, "Accept", "text/html");
    mog_request_set_timeout_ms(req, 10000);

    mog_response *resp = mog_perform(req);
    if (!mog_response_ok(resp)) {
        fprintf(stderr, "request failed: %s\n", mog_response_error_message(resp));
        mog_response_free(resp);
        mog_request_free(req);
        return 1;
    }

    size_t len = 0;
    const char *body = mog_response_body(resp, &len);
    printf("status %d, %zu bytes\n", mog_response_status(resp), len);
    fwrite(body, 1, len, stdout);

    mog_response_free(resp);
    mog_request_free(req);
    return 0;
}
```

Link against the shared library, for example `cc example.c -lmog_c`. On Windows,
define `MOG_C_USE_SHARED` before including the header when you link the import
library.

## Python ctypes example

```python
import ctypes

lib = ctypes.CDLL("libmog_c.so")   # .dylib on macOS, mog_c.dll on Windows
lib.mog_request_new.restype = ctypes.c_void_p
lib.mog_request_new.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
lib.mog_perform.restype = ctypes.c_void_p
lib.mog_perform.argtypes = [ctypes.c_void_p]
lib.mog_response_status.restype = ctypes.c_int
lib.mog_response_status.argtypes = [ctypes.c_void_p]
lib.mog_response_body.restype = ctypes.c_void_p
lib.mog_response_body.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t)]

req = lib.mog_request_new(b"GET", b"https://example.com")
resp = lib.mog_perform(req)
print("status", lib.mog_response_status(resp))

length = ctypes.c_size_t(0)
ptr = lib.mog_response_body(resp, ctypes.byref(length))
body = ctypes.string_at(ptr, length.value)
print(len(body), "bytes")

lib.mog_response_free(resp)
lib.mog_request_free(req)
```

## Request options

Set these on a request handle before calling `mog_perform()`. Each setter is a
no-op when the handle is NULL, and every string argument is copied.

| Function | Purpose |
|----------|---------|
| `mog_request_set_header` | Set or replace a request header |
| `mog_request_set_body` | Raw request body (binary-safe) |
| `mog_request_set_json` | JSON body with a default `application/json` type |
| `mog_request_set_query_param` | Add a query-string parameter |
| `mog_request_set_cookie` | Add a cookie for this request |
| `mog_request_set_timeout_ms` / `mog_request_set_connect_timeout_ms` | Deadlines |
| `mog_request_set_verify_tls` | Toggle TLS certificate verification |
| `mog_request_set_ca_bundle` | Use a PEM CA bundle for TLS trust |
| `mog_request_set_client_cert` | Present a client certificate for mTLS |
| `mog_request_set_basic_auth` / `mog_request_set_bearer_token` / `mog_request_set_digest_auth` | Authentication |
| `mog_request_set_proxy` | HTTP proxy URL |
| `mog_request_set_backend` | Force a backend by name |
| `mog_request_set_allow_redirects` / `mog_request_set_max_redirects` | Redirect policy |
| `mog_request_set_max_response_bytes` | Body size cap (0 = unlimited) |
| `mog_request_set_decompress` | Toggle gzip and deflate decoding |
| `mog_request_set_user_agent` | Override the User-Agent header |

## Response accessors

| Function | Returns |
|----------|---------|
| `mog_response_ok` | 1 if the exchange completed, else 0 |
| `mog_response_error_code` / `mog_response_error_message` | Error category and message |
| `mog_response_status` / `mog_response_reason` | HTTP status and reason phrase |
| `mog_response_url` | Final URL after redirects |
| `mog_response_body` / `mog_response_body_size` | Body bytes and length |
| `mog_response_header_count` / `mog_response_header_name` / `mog_response_header_value` | Iterate headers by index |
| `mog_response_header` | First value for a header name, case-insensitive |
| `mog_response_elapsed_ms` / `mog_response_downloaded_bytes` | Timing and byte count |
| `mog_response_backend` | Name of the backend that served the request |

## Running a server

The C API also exposes the embedded HTTP/S server, so a C or FFI program can
serve files and handle requests. Build a server, register a static mount and/or
route handlers, then start it (non-blocking).

A route handler is a C callback invoked on a worker thread for each matching
request. It receives a borrowed request view and a response builder that are
valid only for the duration of the call. Because handlers run on worker threads,
any shared state a handler touches must be synchronized by the handler.

```c
#include <mog/mog_c.h>
#include <string.h>

static void hello(const mog_server_request *req, mog_server_response *resp, void *user)
{
    (void)req; (void)user;
    mog_server_response_set_status(resp, 200);
    mog_server_response_set_header(resp, "Content-Type", "text/plain");
    const char *body = "hello from a C handler";
    mog_server_response_set_body(resp, body, strlen(body));
}

int main(void)
{
    mog_server *server = mog_server_new();
    mog_server_set_port(server, 8080);
    mog_server_route(server, "GET", "/hello", hello, NULL);
    mog_server_serve_files(server, "/", "./public", 1);
    /* mog_server_use_self_signed_tls(server); for HTTPS */

    if (mog_server_start(server) != 0) {
        fprintf(stderr, "%s\n", mog_server_last_error(server));
        return 1;
    }
    mog_server_wait(server);      /* until mog_server_stop() from another thread */
    mog_server_free(server);
    return 0;
}
```

From Python ctypes, wrap the handler with `CFUNCTYPE` (ctypes acquires the GIL
for the callback) and keep the wrapper object alive for the server's lifetime:

```python
import ctypes
lib = ctypes.CDLL("libmog_c.so")
HANDLER = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p)

lib.mog_server_response_set_status.argtypes = [ctypes.c_void_p, ctypes.c_int]
lib.mog_server_response_set_body.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]

def handle(req, resp, user):
    body = b"hello from python"
    lib.mog_server_response_set_status(resp, 200)
    lib.mog_server_response_set_body(resp, body, len(body))

cb = HANDLER(handle)                       # keep a reference alive
lib.mog_server_new.restype = ctypes.c_void_p
server = lib.mog_server_new()
lib.mog_server_route(server, b"GET", b"/hello", cb, None)
lib.mog_server_start(server)
```

Configure the server with `mog_server_set_bind_address`, `mog_server_set_port`,
and `mog_server_set_threads`. Enable HTTPS with `mog_server_use_self_signed_tls`
(development) or `mog_server_use_tls_files`. Inside a handler, read the request
with `mog_server_request_method` / `_path` / `_query` / `_header` / `_body`, and
build the response with `mog_server_response_set_status` / `_set_header` /
`_set_body`. Manage the lifecycle with `mog_server_start`, `mog_server_port`,
`mog_server_is_running`, `mog_server_stop`, `mog_server_wait`, and
`mog_server_free`.

## Building

The shared library is built by default for top-level builds. Configure with
`-DMOG_BUILD_C_SHARED=ON` when embedding mog in another project. The build
produces `libmog_c.so` on Linux, `libmog_c.dylib` on macOS, and `mog_c.dll` with
an import library on Windows, alongside the installed `mog/mog_c.h` header. See
the [Library](library.html) page for the C++ API and the [Guide](guide.html) for
backend and TLS behavior.
