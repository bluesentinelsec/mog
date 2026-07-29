# mog

Developer guide for this **cppboot** environment. This README orients you to
the build, test, and source-onboarding workflows — not product requirements.

Default library surface is the **version** component driven by the root **`VERSION`** file (single source of truth) with CLI `--version` / `-V`, unit tests, and a small benchmark. Bump `VERSION` only — CMake regenerates the version API. Add real features as new components under `src/<component>/`.


## Prerequisites

- CMake **3.20+** (3.28+ if this project was generated with C++20 modules)
- A **C++20** compiler (GCC, Clang, or MSVC recent enough for C++20)
- GNU Make (or another make that understands this Makefile)
- **Ninja** is required when this project uses C++20 modules (`--with-modules`)
- Optional tools: `clang-format`, `doxygen`, an LSP client with clangd
- Network access on first configure (CMake **FetchContent** downloads pinned deps)


## Continuous integration

GitHub Actions workflows (default; disable with `cppboot --no-github-actions`):

| Workflow | File | Purpose |
|----------|------|---------|
| **CI** | `.github/workflows/ci.yml` | Ubuntu/macOS/Windows × Debug+Release, tests, benches, artifacts |
| **Sanitizers** | `.github/workflows/sanitizers.yml` | Linux ASan+UBSan build + `ctest` (failures fail the job) |
| **Release** | `.github/workflows/release.yml` | Tag `v*` or manual dispatch → notes + zip assets |

**CI** (each OS):

1. Configure and build **Debug** (Ninja)
2. Run unit tests (CTest) on Debug
3. Configure and build **Release**
4. Run unit tests on Release
5. Run Google Benchmark binaries under `build/release` (short min time)
6. Package and upload zip artifacts via `actions/upload-artifact` for **Debug**
   and **Release** on each OS

Artifact archive name shape:

```text
<app>-<os>-<cpu-arch>-<debug|release>-<version>.zip
```

Examples: `myproj-linux-x86_64-debug-0.1.0.zip`,
`myproj-macos-arm64-release-0.1.0.zip`,
`myproj-windows-x86_64-release-0.1.0.zip`.

Each zip contains the contents of `build/<config>/bin/` (app, tests, benches).
Version comes from the root **`VERSION`** file (also what `./<app> --version`
prints after configure).

**Sanitizers** (Ubuntu + Clang): configure with
`-{ctx.macro}_ENABLE_SANITIZERS=ON`, build, run tests with
`ASAN_OPTIONS` / `UBSAN_OPTIONS` set so findings abort (non-zero exit).

Locally on Linux: `make sanitizer` (same flags and env).

### Creating a release

The root **`VERSION`** file is the only place to bump the package version.

1. Edit **`VERSION`** (e.g. `1.0.0`), commit, and merge to the default branch.
2. Either:
   - **Tag:** `git tag -a v1.0.0 -m v1.0.0 && git push origin v1.0.0`
     (tag **must** match `VERSION`), or
   - **Actions → Release → Run workflow** — leave the version input empty to
     use `VERSION`, or pass the same semver (mismatch fails the job).
3. The Release workflow builds **Release** on Linux/macOS/Windows, verifies the
   binary `--version` matches `VERSION`/tag, generates notes, and uploads zips
   named `<app>-<os>-<arch>-release-<version>.zip`.

Action pins use current stable majors (`actions/checkout@v7`,
`actions/upload-artifact@v7`, `actions/download-artifact@v7`,
`lukka/get-cmake@latest`, `softprops/action-gh-release@v3`).


## GitHub Codespaces (VS Code in the browser)

This repo includes a **Dev Container** (`.devcontainer/`) for
[GitHub Codespaces](https://github.com/features/codespaces) and local
VS Code Dev Containers.

### Open in Codespaces

1. Push the repo to GitHub (or use an existing remote).
2. On GitHub: **Code → Codespaces → Create codespace on &lt;branch&gt;**.
3. Wait for the container create hooks:
   - `onCreateCommand`: install Ninja, clang-format, Doxygen, ctags
   - `postCreateCommand`: `cmake --preset debug` + Debug build (FetchContent)
4. In the browser VS Code: **F5** (*Debug mog*), Testing view, or terminal
   `make test` / `./build/debug/bin/mog --version`.

### Local Dev Container

In desktop VS Code with the Dev Containers extension:
**Dev Containers: Reopen in Container**.

Disable with `cppboot --no-codespaces` when regenerating a project.


## Open in VS Code

1. Open this folder in VS Code (`code .`).
2. Install the **recommended extensions** when prompted
   (clangd, CMake Tools, CodeLLDB, **C++ TestMate**).
3. Install **Ninja** (`brew install ninja` / `apt install ninja-build`). VS Code
   presets and `make` both prefer Ninja so they share the same `build/debug` tree.
4. **Build once:** `Ctrl/Cmd+Shift+B` (*Build Debug*). This configures CMake,
   builds, and places `compile_commands.json` at the repo root for **clangd**.
5. If red squiggles remain: Command Palette → **clangd: Restart language server**
   (or reload the window). IntelliSense needs that compilation database.
6. **Debug the app:** F5 → *Debug mog* (rebuilds Debug first).
7. **Unit tests (per-test ▶):** open the **Testing** view (beaker icon).
   After a Debug build, **C++ TestMate** discovers GoogleTest binaries under
   `build/debug/bin/` and offers run/debug for suites and individual `TEST`s.
8. **Bulk tests:** Task *Test*, CMake Tools CTest, or `make test`.

Presets live in `CMakePresets.json` (`debug` → `build/debug`, `release` → `build/release`).

**Generator mismatch:** If you see *generator : Ninja Does not match ... Unix Makefiles*,
the build tree was configured with a different generator. Fix with:

```bash
make reconfigure-debug
# or: rm -rf build/debug && cmake --preset debug
```

Then F5 again. Prefer always using Ninja (install it; both `make` and VS Code will use it).

**IntelliSense note:** Disable Microsoft C/C++ IntelliSense (already set) and use
**clangd** only. Do not enable both. `std::string` / CLI11 / project headers resolve
from `compile_commands.json` after the first successful configure+build.

**Tests note:** TestMate needs built test executables — Build Debug first.
Individual test debug uses CodeLLDB (`lldb`) on macOS/Linux; on Windows you may
set `testMate.cpp.debug.configTemplate.type` to `cppvsdbg`.

Windows: use the *Debug mog (Windows)* launch config (MSVC debugger).

## Preferred third-party libraries

These are **imported by default** via FetchContent (see `cmake/Dependencies.cmake`)
and linked into the project library. Prefer them for the jobs below instead of
adding alternate stacks without a strong reason.

| Purpose | Library | CMake option (default **ON**) | CMake target |
|---------|---------|-------------------------------|--------------|
| CLI arguments | [CLI11](https://github.com/CLIUtils/CLI11) | `-DMOG_WITH_CLI11=OFF` | `CLI11::CLI11` |
| JSON | [nlohmann/json](https://github.com/nlohmann/json) | `-DMOG_WITH_JSON=OFF` | `nlohmann_json::nlohmann_json` |
| Console logging | [spdlog](https://github.com/gabime/spdlog) | `-DMOG_WITH_SPDLOG=OFF` | `spdlog::spdlog` |

Example headers:

```cpp
#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
```

Disable any of them at configure time if you do not need them:

```bash
cmake -S . -B build/debug \
  -DMOG_WITH_CLI11=OFF \
  -DMOG_WITH_JSON=OFF \
  -DMOG_WITH_SPDLOG=OFF
```

## Layout

```text
mog/
  VERSION              # single source of truth for package version
  src/main.cpp         # THE program entrypoint (always here)
  src/<component>/     # library implementation; each dir has CMakeLists.txt
  include/             # public headers (classic layout only; version.hpp is generated)
  tests/<component>/   # GoogleTest / GoogleMock
  benchmarks/<component>/
  cmake/               # shared CMake modules + version.*.in templates
  CMakeLists.txt
  Makefile             # macOS / Linux day-to-day targets
  build.bat            # Windows day-to-day targets (mirrors Makefile)
```

This project uses **classic headers**. Public headers live under `include/mog/` (directory tree == namespace).

**Modules toolchain note:** CMake C++20 modules need **Ninja** (the Makefile
selects it automatically when `ninja` is on `PATH`), **CMake 3.28+**, and a
compiler with module dependency scanning (**Clang 16+**, **GCC 14+**, or
**MSVC 17.4+**). Stock **AppleClang** often cannot scan modules for CMake yet;
use a recent LLVM Clang/GCC/MSVC when building a modules project.

A project-local `.vimrc` was generated (default; disable with `cppboot --no-vim`). Enable `:set exrc` (and ideally `secure`) in your global Vim config to load it.

**VS Code** config is included (default; disable with `cppboot --no-vscode`): open the folder, install recommended extensions, select the **debug** CMake preset, then Build / F5 / Test. See [Open in VS Code](#open-in-vs-code).

**ctags:** `.ctags` is configured for Universal Ctags (default; `cppboot --no-ctags` to skip). Run `make tags` to build the `tags` index.

## Versioning

Package version lives in the root **`VERSION`** file (one line, e.g. `0.1.0`).

| Consumer | How it gets the version |
|----------|-------------------------|
| CMake `project(... VERSION ...)` | Reads `VERSION` at configure time |
| `mog::Version()` / CLI `--version` | Generated from `cmake/version.*.in` |
| GitHub Release workflow | Requires tag / dispatch input to match `VERSION` |
| Doxygen `PROJECT_NUMBER` | Injected from `VERSION` by `make doc` / `build.bat doc` |

**To ship a new version:** edit `VERSION` only, commit, then tag `vX.Y.Z` (or
run the Release workflow). Do not hand-edit generated version sources under the
build tree.

## Using this library from another CMake project

The library target is **`mog::lib`** (also **`mog::mog`**).

When this repo is **not** the top-level CMake project (via `add_subdirectory` or
`FetchContent`), app/tests/benchmarks and optional app deps default **off** so
you only build the library.

### `add_subdirectory`

```cmake
add_subdirectory(path/to/mog)
target_link_libraries(my_app PRIVATE mog::lib)
```

### `FetchContent` (e.g. from GitHub)

```cmake
include(FetchContent)
FetchContent_Declare(
  mog
  GIT_REPOSITORY https://github.com/<org>/mog.git
  GIT_TAG        v0.1.0   # or main / a commit SHA
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(mog)
target_link_libraries(my_app PRIVATE mog::lib)
```

Optional cache variables (prefix `MOG_`):

| Option | Default (top-level / embedded) | Meaning |
|--------|----------------------------------|---------|
| `MOG_BUILD_APP` | ON / OFF | Demo executable |
| `MOG_BUILD_TESTS` | ON / OFF | GoogleTest suite |
| `MOG_BUILD_BENCHMARKS` | ON / OFF | Google Benchmark |
| `MOG_WITH_CLI11` | ON / OFF | CLI11 |
| `MOG_WITH_JSON` | ON / OFF | nlohmann/json |
| `MOG_WITH_SPDLOG` | ON / OFF | spdlog |

### `find_package` (after install)

```bash
cmake --install build/release --prefix /path/to/prefix
```

```cmake
find_package(mog REQUIRED CONFIG)
target_link_libraries(my_app PRIVATE mog::lib)
```

## Build

Out-of-source builds only. Artifacts land under `build/`.

| Command (Unix) | Command (Windows) | Meaning |
|----------------|-------------------|---------|
| `make` / `make debug` | `build.bat` / `build.bat debug` | Configure and build **Debug** |
| `make release` | `build.bat release` | Configure and build **Release** |
| `make test` | `build.bat test` | Unit tests (Debug) |
| `make bench` | `build.bat bench` | Microbenchmarks (Release) |
| `make fmt` | `build.bat fmt` | clang-format |
| `make doc` | `build.bat doc` | Doxygen HTML |
| `make clean` | `build.bat clean` | Remove build trees |

Debug tree: `build/debug`
Release tree: `build/release`

The Debug configure step links/copies `compile_commands.json` at the repo root for LSP.

### Windows

Prefer **`build.bat`** (same targets as the Makefile). Open a **Developer
Command Prompt for VS** or ensure `cmake` (and ideally `ninja`) are on `PATH`:

```bat
build.bat
build.bat test
build.bat release
build.bat help
```

WSL / MSYS2 can keep using `make` if you prefer.

## Run the app

The program entrypoint is always **`src/main.cpp`**.
`make` links **`./mog`** at the project root to `build/debug/bin/mog`.

After `make debug`:

```bash
./mog
./mog --version
./mog -V
```

## Tests

```bash
make test
```

Runs CTest against the Debug build (includes version API tests).

## Benchmarks

```bash
make bench
```

Builds **Release** and runs Google Benchmark binaries (short min time for smoke runs).

## Format

```bash
make fmt
```

Runs **clang-format** with the checked-in `.clang-format` (**Microsoft** style).

**Formatting** uses Microsoft clang-format; **code logic/naming/design** follow
the **Google C++ Style Guide** (see AGENTS.md).

## Documentation

```bash
make doc
```

Runs **Doxygen** using the checked-in `Doxyfile`. HTML output: `docs/html/`.

## Clean

```bash
make clean
```

Removes `build/`, generated docs, and the root `compile_commands.json` link.

## LSP / clangd

- CMake exports a compilation database (`CMAKE_EXPORT_COMPILE_COMMANDS=ON`).
- After `make debug`, `compile_commands.json` is available at the **project root**.
- Open the project root in your editor so clangd picks it up.
- See `.clangd` for the minimal clangd config.

## Compiler warnings

This project compiles with **warnings as errors** (`-Wall -Wextra -Wpedantic -Werror`
on GCC/Clang; `/W4 /WX` on MSVC). Fix warnings instead of silencing them.

## Onboarding new source files

Sources are organized by **logical component**. Each component directory has its
own `CMakeLists.txt` and **lists files explicitly** (no `file(GLOB)`).

### Example: add a `parser` component

1. **Public header** (classic layout):

   `include/mog/parser.hpp`

2. **Implementation**:

   ```text
   src/parser/
     CMakeLists.txt
     parser.cpp
   ```

3. **`src/parser/CMakeLists.txt`**:

   ```cmake
   target_sources(${PROJECT_NAME}_lib
     PRIVATE
       parser.cpp
   )
   ```

4. **Register the component** in `src/CMakeLists.txt`:

   ```cmake
   add_subdirectory(version)
   add_subdirectory(parser)
   ```

5. **Tests** (same pattern):

   ```text
   tests/parser/
     CMakeLists.txt
     parser_test.cpp
   ```

   And `add_subdirectory(parser)` in `tests/CMakeLists.txt`.

### Rules of thumb

- The only program entrypoint is **`src/main.cpp`**. Keep it thin; put logic in library components.
- Library code goes under `src/<component>/`, never into `src/main.cpp` beyond startup wiring.
- List every `.cpp` / `.cppm` in the component `CMakeLists.txt`.
- Mirror component names under `tests/` and `benchmarks/`.
- Do not rely on directory globs for sources — explicit lists keep reviews and CI unambiguous.

## Agent / contributor conventions

See **[AGENTS.md](AGENTS.md)** for coding standards, documentation rules, and
how automated agents (and humans) should work in this repository.

## License

See `LICENSE` (mit).
