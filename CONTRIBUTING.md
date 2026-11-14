# Contributing

Contributions to keyboard-volume-app are welcome, including bug reports,
feature suggestions, documentation, translations and code.

## Issues

Search the [existing issues](https://github.com/Adiker/keyboard-volume-app/issues)
before opening one. For a bug report, include:

- The application version, Linux distribution, desktop environment and whether
  the session uses Wayland or X11.
- Steps to reproduce, the expected result and what happened instead.
- Relevant logs or screenshots, with personal information removed.

For a feature suggestion, describe the problem and how the proposed behavior
would help. If useful, include your audio setup or input device details.

## Preparing a change

1. Fork the repository if you do not have write access. Start a branch from the
   latest upstream `main`; use a `feature/`, `fix/`, `refactor/`, `docs/` or
   `chore/` prefix. Do not commit directly to `main`.
2. Make the change and update relevant documentation and English/Polish
   translations when needed. The [installation guide](docs/en/installation.md)
   lists build dependencies, and [ARCHITECTURE.md](ARCHITECTURE.md) covers the
   build, project internals and test recipes.
3. Run the checks relevant to the change. For changed C++ files under `cpp/src`
   or `cpp/tests`, run `clang-format --dry-run --Werror` on those files. See
   [Tests](ARCHITECTURE.md#tests) for CTest and isolated integration tests.
4. Push your branch and open a pull request targeting `main`. Explain the change
   and the checks performed. For changes involving evdev, libpulse, D-Bus,
   MPRIS, threading, CMake or config migration, include a short risk and
   rollback note.

CI builds and tests Debug and Release configurations for relevant paths and
checks formatting of changed C++ files. The workflow is path-filtered, so
documentation-only changes do not run that CI workflow. See the
[test documentation](ARCHITECTURE.md#tests) for details.
