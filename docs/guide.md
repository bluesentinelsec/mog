---
title: Guide
description: "How mog works: backend selection and fallback, TLS trust, request behavior, and static or scratch deployment."
---

# Guide

This page explains how mog behaves under the hood: backend selection, TLS trust,
request semantics, and shipping a single static binary.

## Backends

mog exposes one API over several HTTP stacks. Selection follows this precedence:

1. **Explicit.** `Options::backend` or the CLI `--backend` is honored exactly.
2. **Environment.** `MOG_BACKEND` is used next.
3. **`Auto`** (the default). It prefers the platform-native backend and otherwise falls back to **embedded**.

| Backend | Selector | Role |
|---------|----------|------|
| macOS NSURLSession | `native` | Auto default on macOS |
| libcurl (Linux & macOS, runtime `dlopen`) | `curl` | Auto default on Linux |
| Windows WinHTTP | `winhttp` | Auto default on Windows |
| Embedded (HTTP/1.1 + mbedTLS) | `embedded` | Always-present fallback |

Native libraries are reached without hard-linking. libcurl is loaded at runtime
with `dlopen`, and WinHTTP and NSURLSession are always-present system frameworks.
If a native library is missing, for example on a `scratch` container without
libcurl, `Auto` lands on embedded automatically.

### Capability-aware fallback

`Auto` chooses **per request**. If a request needs a feature the native backend
does not implement, mog transparently uses embedded, so the default never
silently loses a feature. The native backends are at parity for streaming,
`max_response_bytes`, and Digest auth. The one remaining difference is trust
configuration.

A PEM CA bundle (`--cacert`) or a PEM client certificate (`--cert`) with
NSURLSession or WinHTTP falls back to a PEM-capable backend. Both curl and
embedded honor these. This is by design. Native backends verify against the OS
trust store, so supplying PEM material means you want file-based trust.

Choosing a backend explicitly disables the fallback. That request uses exactly
the named backend.

## TLS trust

When verification is on (the default), the embedded backend resolves CA roots in
this order, and the first success wins:

1. **CLI or Options.** `--cacert` or `Options::ca_bundle`.
2. **Environment.** `MOG_CA_BUNDLE`, then `SSL_CERT_FILE`, `REQUESTS_CA_BUNDLE`, `CURL_CA_BUNDLE`, then `SSL_CERT_DIR`.
3. **System.** Common OS trust locations. On Windows, the CryptoAPI store is read via a runtime-loaded `crypt32.dll`.
4. **Embedded.** The Mozilla CA bundle compiled into the binary.
5. **Fail loud.** A clear error describes how to supply a bundle.

This is why HTTPS verifies even on a minimal image with no CA files: the Mozilla
roots ship inside the binary. Set `MOG_NO_EMBEDDED_CA=1` to forbid that fallback.
Native backends use the OS trust store directly.

## Request behavior

- **Redirects** are followed by default, up to 5. Use `--max-redirs` to change the limit, or `--no-location` to disable following. A `301` or `302` on a `POST` becomes a `GET`, and a `303` becomes a `GET`.
- **Cookies.** `Session` keeps a jar scoped by domain, path, and the `Secure` flag. It stores `Set-Cookie` and replays matching cookies on later requests.
- **Compression.** `gzip` and `deflate` are advertised and decoded by default. Use `--no-decompress` to opt out.
- **Streaming.** `-o FILE` on the CLI, or `Options::response_writer` in the library, delivers the body incrementally, so large responses use constant memory.
- **Timeouts.** `--timeout` bounds each I/O operation, and `--connect-timeout` bounds the connect phase.
- **Body cap.** `max_response_bytes` in the library rejects oversized responses.

## Static and scratch deployment

A fully static, self-contained binary runs on `FROM scratch` with nothing else on
the filesystem. The C runtime, the C++ runtime, mbedTLS, and the Mozilla CA
bundle are all compiled in.

```bash
docker build -f docker/Dockerfile.linux-static -t mog-static .
docker run --rm mog-static get https://example.com   # HTTPS via bundled CAs, no CA file on disk
```

Build it on **Alpine (musl)**. A glibc `-static` build is not scratch-safe,
because the glibc resolver loads NSS plugins at runtime with `dlopen`, and those
plugins do not exist on `scratch`, so DNS breaks. The musl resolver is built in.
DNS still needs a resolver config, which container runtimes provide via
`/etc/resolv.conf`.

## Non-goals

- **HTTP/2** is not implemented in the embedded stack, which is HTTP/1.1 only. Use a native backend, which speaks HTTP/2 where the OS library does.
- **WebSocket** is out of scope. For server-push, a streaming response covers many cases.
