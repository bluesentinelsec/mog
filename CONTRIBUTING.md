# Contributing to mog

Thank you for considering a contribution. This guide is intentionally generic
and works for most open-source C++ projects bootstrapped with cppboot.

## Code of conduct

Be mature, respectful, and technical. See [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).

## Getting started

1. Fork the repository and clone your fork.
2. Install a C++20 toolchain, CMake 3.20+, Ninja (recommended), and Make.
3. Build and test:

   ```bash
   make
   make test
   ./mog --version
   ```

4. Optional: open the folder in VS Code and install recommended extensions
   (see README).

## Development workflow

- Prefer small, focused pull requests.
- Follow [AGENTS.md](AGENTS.md) for layout, style, and documentation rules:
  - **Google C++ Style Guide** for code logic and naming
  - **Microsoft** `.clang-format` for formatting (`make fmt`)
- List new sources explicitly in the component `CMakeLists.txt` (no globs).
- Add or update unit tests for behavior changes.
- Keep `make` and `make test` green. On Linux, `make sanitizer` is encouraged
  for memory/UB issues.

## Reporting bugs and proposing features

- Use GitHub Issues for bugs and feature requests.
- Include OS, compiler, CMake version, and steps to reproduce when filing bugs.
- Search existing issues before opening a new one.

## Security vulnerabilities

Do **not** open a public issue for security problems. See
[SECURITY.md](SECURITY.md).

## Pull request checklist

- [ ] Code builds (`make`)
- [ ] Tests pass (`make test`)
- [ ] Formatted (`make fmt`)
- [ ] Public APIs have Doxygen where appropriate
- [ ] Commit messages are clear; PR description explains *why*

## License

By contributing, you agree that your contributions will be licensed under the
same license as this repository (see [LICENSE](LICENSE)).
