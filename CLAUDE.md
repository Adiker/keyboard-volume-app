# CLAUDE.md — keyboard-volume-app

This is the concise working guide for Claude in this repository. Mandatory agent
rules live in `AGENTS.md`; treat that file as authoritative for git workflow,
branch hygiene, PR expectations, and implementation guardrails.

## Project Snapshot

- **What:** Linux desktop utility that captures volume/mute hotkeys through evdev
  and applies them to a selected audio application instead of the system master
  volume.
- **Stack:** C++20, Qt6 Widgets, Qt6 DBus, CMake 3.20+.
- **Audio:** PulseAudio-compatible IPC via libpulse, with PipeWire support through
  libpipewire.
- **Input:** libevdev + libuinput.
- **Platform:** Linux only. KDE Plasma is the primary target; Wayland OSD support
  uses a guarded native layer-shell path with an XWayland fallback.

## Read First

- `AGENTS.md` — mandatory workflow and sharp edges for agents. Read before
  changing code, creating branches, committing, pushing, or opening PRs.
- `ARCHITECTURE.md` — full technical reference: project structure, module map,
  config schema, signal flow, threading, D-Bus/MPRIS, packaging, tests, and key
  conventions.
- `README.md` — user-facing setup, usage, configuration, troubleshooting, and
  `kv-ctl` / `qdbus` examples.
- `ROADMAP.md` — backlog, planned work, and historical review notes.

## Common Commands

Build:

```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build -j$(nproc)
```

Run:

```bash
cpp/build/keyboard-volume-app
```

Test:

```bash
cmake -S cpp -B cpp/build -DBUILD_TESTING=ON
cmake --build cpp/build -j$(nproc)
cd cpp/build && ctest -E test_mprisclient --output-on-failure
cd cpp/build && dbus-run-session -- ctest -R test_mprisclient --output-on-failure
```

For C++ changes under `cpp/src` or `cpp/tests`, run `clang-format --dry-run
--Werror` on the changed files before PR. If it fails, format only those changed
files.

## Claude Working Notes

- Do not duplicate architectural detail here. Add durable technical reference
  material to `ARCHITECTURE.md`.
- Put end-user behavior, install steps, CLI examples, and troubleshooting in
  `README.md`.
- Put agent-only workflow mistakes or non-obvious guardrails in `AGENTS.md`.
- Documentation-only changes do not require a C++ build or CTest run; still run
  lightweight Markdown/reference checks when moving docs.
- The user must be in the `input` group for evdev access when running the app:
  `sudo usermod -aG input $USER`, then log out and back in.
