---
title: Web Build and CI
description: "Maintain the mog Emscripten build, browser tests, CI job, and release package."
---

# Web build and CI

This page is for mog maintainers. For the public browser contract and
application integration instructions, see [Web / Emscripten](web.html).

## Prerequisites

Install and activate an Emscripten SDK so `emcc`, `emcmake`, and `emrun` are on
`PATH`. The build also requires CMake 3.20 or newer, a C++20-capable Emscripten
toolchain, and a browser that `emrun` can launch. Ninja is preferred but not
required by the Makefile wrapper.

The upstream [Emscripten SDK instructions](https://emscripten.org/docs/getting_started/downloads.html)
describe installation and activation. A typical existing checkout is activated
for the current shell with:

```bash
source /path/to/emsdk/emsdk_env.sh
```

Google Chrome is the browser used by CI. If the Makefile cannot find Ninja, it
uses CMake's default generator; set `GENERATOR="Unix Makefiles"` explicitly when
needed.

## Build and test locally

Use the repository wrappers for normal work:

```bash
make web          # configure and build Release library + browser test executable
make web-test     # build, then run the browser tests in headless Chrome
make web-package  # build and create the installable Release zip
```

`make web` writes to `build/web`. It enables `MOG_BUILD_TESTS` and disables the
native-only app, benchmarks, shared C API, and optional CLI/JSON/logging stacks.
It builds `mog_web_test.html` but does not run it; `make web-test` launches it
through `emrun`.

The equivalent commands for a debuggable build are:

```bash
emcmake cmake -S . -B build/web-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DMOG_BUILD_TESTS=ON \
  -DMOG_WITH_CLI11=OFF \
  -DMOG_WITH_JSON=OFF \
  -DMOG_WITH_SPDLOG=OFF
cmake --build build/web-debug --parallel
emrun --browser=google-chrome \
  --browser_args="--headless=new --no-sandbox --disable-gpu" \
  --kill_exit --timeout 120 build/web-debug/bin/mog_web_test.html
```

Run `make test` as well before submitting a change. That native suite remains
the full HTTP behavioral contract; the browser suite covers the web-specific
transport and platform exclusions.

## What the browser test proves

`tests/web/web_test.cpp` is compiled to HTML, JavaScript, and Wasm. `emrun`
serves the test output and `tests/web/web_fixture.txt` from one local origin, so
the test performs a real browser Fetch without depending on the public network.
It verifies:

- automatic selection and availability of `Backend::Web`;
- a successful same-origin Fetch and web response metadata;
- `max_response_bytes` enforcement;
- rejection of browser-controlled TLS options; and
- the clear `UnsupportedBackend` result from `mog::Server`.

Keep browser tests local and deterministic. Cross-origin tests would require a
second controlled origin and explicit CORS behavior; do not add a public API
dependency to CI.

## CI coverage

The `web` job in `.github/workflows/ci.yml` runs independently of the native OS
matrix. It:

1. installs CMake, Ninja, and the Emscripten SDK;
2. configures, builds, and runs the browser tests in Debug;
3. repeats the build and browser tests in Release;
4. installs the Release library and creates the consumer zip; and
5. uploads that zip as a workflow artifact.

The tests run in headless Chrome through `emrun`. Both compilation and the final
application-style link are important: the latter verifies that the exported
`mog::lib` target propagates `-sASYNCIFY=1`.

## Release package

`make web-package` creates:

```text
build/web/package/mog-web-wasm32-release-<version>.zip
```

The zip contains one top-level directory with:

```text
mog-web-wasm32-release-<version>/
├── EMSCRIPTEN_VERSION
├── LICENSE
├── include/mog/...
└── lib/
    ├── libmog.a
    └── cmake/mog/...
```

There is deliberately no prelinked `.wasm` or JavaScript loader. mog is a
static C++ library; the consumer's final Emscripten link produces those files
and applies Asyncify to the complete program.

The `build-web` job in `.github/workflows/release.yml` repeats the Release build
and browser test, creates the same package using the release version, and makes
the zip available to the publishing job. `EMSCRIPTEN_VERSION` records the SDK
used to build the archive so callers can select a compatible toolchain.

## Maintainer checklist

When web transport behavior changes:

1. update `docs/web.md` if the caller-visible contract changed;
2. add or update a deterministic case in `tests/web/web_test.cpp`;
3. run `make web-test` and `make test`;
4. run `make web-package` when CMake export or install behavior changed; and
5. confirm the package still propagates Asyncify through `mog::lib`.
