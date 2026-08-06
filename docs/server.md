---
title: Server
description: "The embedded mog HTTP/S server: the mog serve CLI and the mog::Server library API."
---

# Server

mog includes a small, thread-safe HTTP/1.1 server that follows the same
principles as the client. It has no external dependencies, links statically, and
serves HTTPS using the bundled mbedTLS. There is a `mog serve` command for
serving a directory, and a `mog::Server` library API for building real
applications with routes and handlers.

The server is available on native platforms, including Android and iOS. An Emscripten build is a
browser client and `Server::start()` returns `UnsupportedBackend`; see the
[Web / Emscripten guide](web.html). On native platforms the server uses the
embedded implementation only. Unlike the client, it does not have OS-native
backends, because there is no ubiquitous cross-platform native server library to
load. This gives native platforms one implementation with the same
zero-dependency, static-link story as the rest of mog.

On Android the server is part of the Prefab AAR. The application owns Android
lifecycle and background-service integration; see the [Android guide](android.html).
On iOS it is part of the XCFramework, and the application owns lifecycle,
background execution, and Local Network permission; see the [iOS guide](ios.html).

## CLI: `mog serve`

Serve the current directory over HTTP on port 8000:

```bash
mog serve
```

Serve a specific directory and port, bound to all interfaces:

```bash
mog serve ./public --port 8080 --bind 0.0.0.0
```

Serve over HTTPS with an ephemeral self-signed certificate, which is handy for
local development:

```bash
mog serve ./public --self-signed
```

Serve over HTTPS with a real certificate and key:

```bash
mog serve ./public --tls-cert cert.pem --tls-key key.pem
```

Press Ctrl-C to stop. The server prints the address it is listening on.

| Flag | Purpose |
|------|---------|
| `directory` | Directory to serve (positional; default is the current directory) |
| `--port` | Port to listen on (default 8000) |
| `--bind` | Interface to bind (default 127.0.0.1; use 0.0.0.0 for all IPv4) |
| `--threads` | Worker threads (0 uses the hardware concurrency) |
| `--self-signed` | Serve HTTPS with an ephemeral self-signed certificate |
| `--tls-cert` / `--tls-key` | Serve HTTPS with a certificate chain and private key (PEM) |
| `--no-listing` | Disable directory listings |

Static serving includes MIME types, directory listings, index files, Range
requests for partial downloads, and `If-Modified-Since` handling. Requests that
try to escape the served directory are rejected.

## Library: `mog::Server`

Register handlers and static mounts, then start the server. `start()` is
non-blocking; it binds and spawns the worker pool and returns.

```cpp
#include <mog/server.hpp>

mog::ServerOptions opt;
opt.port = 8443;
opt.threads = 8;
opt.tls = *mog::TlsServerConfig::SelfSigned();   // or FromFiles("cert.pem","key.pem")

mog::Server server(opt);

server.route(mog::Method::Get, "/hello", [](const mog::ServerRequest& req) {
    return mog::ServerResponse::Text(200, "hello " + req.header("X-Name"));
});

server.route(mog::Method::Post, "/echo", [](const mog::ServerRequest& req) {
    return mog::ServerResponse::Json(200, req.body);
});

server.serve_files("/", "./public");             // static files + directory listing

auto started = server.start();
if (!started) { /* started.error() */ }
server.wait();                                    // block until stop() is called
```

### Request and response

`ServerRequest` gives you the method, decoded path, query parameters, headers,
body, and the client address. `ServerResponse` carries the status, headers, and
body, with factories for common cases.

```cpp
mog::ServerResponse::Text(200, "hi");
mog::ServerResponse::Json(200, "{\"ok\":true}");
mog::ServerResponse::NotFound();
mog::ServerResponse::Redirect(302, "/login");
auto file = mog::ServerResponse::File("/path/to/report.pdf");   // streams from disk
```

For large or generated bodies, set a streaming producer instead of buffering.
When you do not set a `Content-Length`, the response is sent with chunked
transfer-encoding.

```cpp
mog::ServerResponse r;
r.status_code = 200;
r.set_header("Content-Type", "text/plain");
r.body_producer = [](const mog::ResponseSink& sink) -> mog::Result<void> {
    for (int i = 0; i < 1000; ++i) {
        auto w = sink("line " + std::to_string(i) + "\n");
        if (!w) return w;   // client disconnected
    }
    return mog::Result<void>::Ok();
};
```

### Routing

Dispatch precedence is exact method and path routes first, then static mounts by
longest matching prefix, then the default handler (a 404 if none is set). A HEAD
request falls back to the matching GET route and omits the body.

### TLS

`TlsServerConfig::FromFiles` loads a certificate chain and private key from PEM
files. `TlsServerConfig::SelfSigned` generates an ephemeral EC certificate at
startup, which is meant for development and testing. Clients connecting to a
self-signed server must skip verification, since nothing else trusts it. With
mog's client that is `Options::verify_tls = false`, or `-k` on the CLI.

## Configuration

`ServerOptions` controls the bind address and port, worker thread count, the
listen backlog, read, write, and keep-alive timeouts, header and body size
limits, the maximum number of requests per keep-alive connection, and the
`Server` header value.

## Threading and safety

The server uses a bounded thread pool with blocking I/O. An accept thread hands
connections to worker threads that run keep-alive request loops. The framework
is thread-safe. Routes and static mounts are frozen when `start()` is called, so
request dispatch needs no locking. Because handlers run on worker threads, a
handler that touches shared state must synchronize that state itself.

## Non-goals

Consistent with the client, the server does not implement HTTP/2 or WebSocket,
and it does not use an async event loop. It is a straightforward, competent
HTTP/1.1 server for serving files and building services.
