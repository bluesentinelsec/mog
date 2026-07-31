---
title: CLI Manual
description: Complete reference for every mog command-line flag, exit codes, environment variables, and write-out tokens.
---

# CLI Manual

`mog` is a curl-style command-line HTTP/S client. This page documents every flag,
the exit codes, environment variables, and the `-w` format tokens. For how the
program behaves (backend selection, TLS trust, redirects, streaming), see the
**[Guide](guide.html)**.

## Synopsis

```text
mog <method> <url> [options]      # subcommand form:  mog get https://example.com
mog [options] <url>               # bare form:        mog -X POST https://example.com -d @body.json
mog -I <url>                      # HEAD request
mog --version
```

Methods available as subcommands: `get`, `post`, `put`, `patch`, `delete`, `head`.
The method may also be set with `-X/--request` in the bare form. If a request has
a body (`-d`, `--json`, `-F`) and no explicit method, `mog` promotes `GET` to
`POST`.

## Request options

| Flag | Argument | Description |
|------|----------|-------------|
| `-X, --request` | `METHOD` | HTTP method for the bare form (default `GET`). |
| `-I, --head` | (none) | Issue a `HEAD` request (headers only, no body). |
| `-H, --header` | `Name: value` | Add a request header. Repeatable. A caller-set header wins over mog's default of the same name. |
| `-d, --data` | `DATA` | Request body. `@file` reads the body from a file. Implies `POST` if the method is `GET`. |
| `--json` | `DATA` | JSON body; sets `Content-Type: application/json`. `@file` reads from a file. Implies `POST`. |
| `-F, --form` | `part` | `multipart/form-data` part. Repeatable. See [multipart forms](#multipart-forms). |
| `-G, --get` | (none) | Send `-d` data as a query string on a `GET` instead of a body. |
| `-b, --cookie` | `"a=1; b=2"` | Cookies to send, as a `Cookie` header value. |
| `-A, --user-agent` | `UA` | `User-Agent` header (default `mog/<version>`). |
| `-e, --referer` | `URL` | `Referer` header. |

### Multipart forms

`-F` builds a `multipart/form-data` body (curl-compatible):

| Form | Meaning |
|------|---------|
| `-F name=value` | Text field. |
| `-F name=@path` | File upload; filename defaults to the path's basename, content-type guessed by extension. |
| `-F name=@path;type=image/png;filename=pic.png` | File upload with explicit content-type and/or filename. |
| `-F name=<path` | Text field whose value is read from a file. |

> For an `application/x-www-form-urlencoded` body, use `-d 'a=1&b=2'`.

## Authentication

| Flag | Argument | Description |
|------|----------|-------------|
| `-u, --user` | `user:password` | Basic authentication (or the credentials for `--digest`). |
| `--digest` | (none) | Use HTTP Digest auth with the `-u` credentials (challenge/response; sent after a `401`). |
| `--bearer` | `TOKEN` | `Authorization: Bearer <TOKEN>`. |

## TLS options

| Flag | Argument | Description |
|------|----------|-------------|
| `-k, --insecure` | (none) | Disable TLS certificate verification. Debugging only. |
| `--cacert` | `PATH` | PEM CA bundle to trust (highest precedence). Routes to a PEM-capable backend under `Auto`. |
| `-E, --cert` | `PATH` | Client certificate PEM for mutual TLS (mTLS). |
| `--key` | `PATH` | Client private-key PEM (defaults to `--cert` when omitted). |
| `--pass` | `PHRASE` | Passphrase for an encrypted `--key`. |

See [TLS trust](guide.html#tls-trust) for how CA roots are resolved.

## Connection options

| Flag | Argument | Description |
|------|----------|-------------|
| `-x, --proxy` | `http://host:port` | Route the request through an HTTP proxy (HTTPS targets use `CONNECT`). |
| `--timeout` | `SECONDS` | Overall I/O timeout (default `30`). |
| `--connect-timeout` | `SECONDS` | Connect-only timeout (defaults to `--timeout`). |
| `--max-redirs` | `N` | Maximum redirects to follow (default `5`). |
| `--no-location` | (none) | Do not follow redirects; return the `3xx` response as-is. |

## Output options

| Flag | Argument | Description |
|------|----------|-------------|
| `-o, --output` | `FILE` | Write the response body to a file. Streamed to disk (constant memory). `/dev/null` discards it. |
| `-D, --dump-header` | `FILE` | Write the response headers to a file. |
| `-i, --include` | (none) | Include response headers before the body in the output. |
| `-f, --fail` | (none) | Exit non-zero (`22`) on HTTP `4xx`/`5xx` instead of printing the error body. |
| `-w, --write-out` | `FORMAT` | After the transfer, write `FORMAT` to stderr with tokens expanded. See [write-out tokens](#write-out-tokens). |

## Content options

| Flag | Argument | Description |
|------|----------|-------------|
| `--no-decompress` | (none) | Do not advertise or decode `Content-Encoding` (`gzip`/`deflate`); deliver the body as-is. |

## Backend selection

| Flag | Argument | Description |
|------|----------|-------------|
| `--backend` | `NAME` | Force a backend: `auto` (default), `embedded`, `curl`, `winhttp`, `native`. Overrides `MOG_BACKEND`. |

`auto` prefers the platform-native backend and falls back to embedded. See
[Backends](guide.html#backends). An explicit choice is used exactly, with no fallback.

## Logging options

| Flag | Argument | Description |
|------|----------|-------------|
| `-v, --verbose` | (none) | Debug-level logging (request/response detail). |
| `-s, --silent` | (none) | Silent: suppress logs (the body is still written to output). |
| `-S, --show-error` | (none) | Show error messages even under `--silent`. |
| `--log-level` | `LEVEL` | Explicit level: `trace \| debug \| info \| warn \| error \| critical \| off`. Overrides `-v`/`-s`. |

## Misc

| Flag | Description |
|------|-------------|
| `-V, --version` | Print the version and exit. |

## Write-out tokens

Used with `-w`; expanded and written to **stderr** after the transfer:

| Token | Expands to |
|-------|-----------|
| `%{http_code}` | Final HTTP status code. |
| `%{url_effective}` | Final URL after redirects. |
| `%{time_total}` | Total time in seconds. |
| `%{size_download}` | Bytes of body received. |
| `%{num_redirects}` | Number of redirects followed. |

## Exit codes

| Code | Meaning |
|------|---------|
| `0` | Success (the request completed; a `4xx`/`5xx` is still `0` unless `-f`). |
| `1` | Request/transport error (DNS, connect, TLS, I/O, too many redirects, file error). |
| `2` | Usage / argument error (bad flag, missing URL, unknown backend). |
| `22` | With `-f`, the server returned `4xx`/`5xx`. |

## Environment variables

| Variable | Effect |
|----------|--------|
| `MOG_BACKEND` | Default backend (`auto`/`embedded`/`curl`/`winhttp`/`native`); `--backend` overrides. |
| `MOG_CA_BUNDLE` | PEM CA bundle path (embedded backend trust). |
| `SSL_CERT_FILE`, `REQUESTS_CA_BUNDLE`, `CURL_CA_BUNDLE` | Additional CA bundle env fallbacks. |
| `SSL_CERT_DIR` | Colon-separated directories of PEM CA files. |
| `MOG_NO_EMBEDDED_CA` | Set to forbid the compiled-in Mozilla bundle fallback. |

## Examples

```bash
# GET with response headers shown
mog get https://example.com -i

# POST JSON, print only the status code
mog post https://api.example.com/things --json '{"name":"mog"}' -w '%{http_code}\n' -s -o /dev/null

# Upload a file as multipart, plus a text field
mog post https://api.example.com/upload -F field=value -F file=@./report.pdf

# Download a large file straight to disk (streamed)
mog get https://example.com/big.iso -o big.iso

# Digest auth
mog get https://api.example.com/protected -u user:pass --digest

# Fail the shell on a 4xx/5xx, quietly
mog get https://api.example.com/health -f -s -o /dev/null && echo up

# Force the embedded backend and a custom CA bundle
mog get https://internal.example --backend embedded --cacert /etc/ssl/corp.pem
```
