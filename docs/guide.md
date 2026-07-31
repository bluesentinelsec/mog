---
title: Guide
description: How mog works — backend selection and fallback, TLS trust, request behavior, and static/scratch deployment.
---

# Guide

How mog behaves under the hood: backend selection, TLS trust, request semantics,
and shipping a single static binary.

## Backends

mog exposes one API over several HTTP stacks. Selection precedence:

1. **Explicit** — `Options::backend` / CLI `--backend` (honored exactly).
2. **Environment** — `MOG_BACKEND`.
3. **`Auto`** (default) — prefer the platform-native backend, else fall back to **embedded**.

| Backend | Selector | Role |
|---------|----------|------|
| macOS NSURLSession | `native` | Auto default on macOS |
| libcurl (Linux & macOS, runtime `dlopen`) | `curl` | Auto default on Linux |
| Windows WinHTTP | `winhttp` | Auto default on Windows |
| Embedded (HTTP/1.1 + mbedTLS) | `embedded` | Always-present fallback |

Native libraries are reached **without hard-linking**: libcurl via runtime
`dlopen`, WinHTTP/NSURLSession via always-present system frameworks. If a native
library is missing (e.g. a `scratch` container without libcurl), `Auto` lands on
embedded automatically.

### Capability-aware fallback

`Auto` chooses **per request**: if a request needs a feature the native backend
doesn't implement, mog transparently uses embedded, so the default never silently
loses a feature. The native backends are at parity for streaming,
`max_response_bytes`, and Digest auth. The one remaining difference is trust
configuration:

- A **PEM CA bundle** (`--cacert`) or **PEM client certificate** (`--cert`) with
  NSURLSession/WinHTTP falls back to a PEM-capable backend (curl and embedded
  honor both). This is by design — native backends verify against the OS trust
  store; supplying PEM material means you want file-based trust.

Choosing a backend explicitly disables the fallback — that request uses exactly
the named backend.

## TLS trust

When verification is on (the default), the embedded backend resolves CA roots in
this order — first success wins:

1. **CLI / Options** — `--cacert` / `Options::ca_bundle`.
2. **Environment** — `MOG_CA_BUNDLE`, then `SSL_CERT_FILE`, `REQUESTS_CA_BUNDLE`,
   `CURL_CA_BUNDLE`, then `SSL_CERT_DIR`.
3. **System** — common OS trust locations (on Windows, the CryptoAPI store via a
   runtime-loaded `crypt32.dll`).
4. **Embedded** — the Mozilla CA bundle compiled into the binary.
5. **Fail loud** — a clear error describing how to supply a bundle.

This is why HTTPS verifies even on a minimal image with no CA files: the Mozilla
roots ship inside the binary. Set `MOG_NO_EMBEDDED_CA=1` to forbid that fallback.
Native backends use the OS trust store directly.

## Request behavior

- **Redirects** are followed by default (max 5; `--max-redirs`, disable with
  `--no-location`). `301/302` on a `POST` become `GET`; `303` becomes `GET`.
- **Cookies** — `Session` keeps a jar scoped by domain/path and the `Secure`
  flag; it stores `Set-Cookie` and replays matching cookies on later requests.
- **Compression** — `gzip`/`deflate` are advertised and decoded by default
  (`--no-decompress` to opt out).
- **Streaming** — `-o FILE` (CLI) or `Options::response_writer` (library)
  delivers the body incrementally, so large responses use constant memory.
- **Timeouts** — `--timeout` bounds each I/O; `--connect-timeout` bounds connect.
- **Body cap** — `max_response_bytes` (library) rejects oversized responses.

## Static / scratch deployment

A fully static, self-contained binary — C runtime, C++ runtime, mbedTLS, and the
Mozilla CA bundle all compiled in — runs on `FROM scratch` with nothing else on
the filesystem:

```bash
docker build -f docker/Dockerfile.linux-static -t mog-static .
docker run --rm mog-static get https://example.com   # HTTPS via bundled CAs, no CA file on disk
```

Build it on **Alpine (musl)**. A glibc `-static` build is *not* scratch-safe —
glibc's resolver `dlopen`s NSS plugins at runtime, which don't exist on
`scratch`, so DNS breaks; musl's resolver is built in. (DNS still needs a
resolver config, which container runtimes provide via `/etc/resolv.conf`.)

## Non-goals

- **HTTP/2** is not implemented in the embedded stack (HTTP/1.1 only) — use a
  native backend, which speaks HTTP/2 where the OS library does.
- **WebSocket** is out of scope; for server-push, a streaming response covers
  many cases.
