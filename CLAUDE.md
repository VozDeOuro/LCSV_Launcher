# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Is

**LCSV Prism** — a fork of [Prism Launcher](https://prismlauncher.org) (a Minecraft launcher), customized for the LCSV community. The upstream is PrismLauncher/PrismLauncher on `develop`. LCSV-specific additions live under the `LCSV` namespace; the rest is upstream Prism C++/Qt6.

The core LCSV feature: a custom pack distribution system where packs are served as `.mrpack` files from `https://mc.lcsv.com.br`, with a content-addressable file store on Tower (`/mnt/user/appdata/nginx/mc/`). See `LCSV_PLAN.md` for full design.

## Build

**Dependencies:** CMake ≥ 3.25, Qt6, Ninja, Java (for javacheck/launcher jars), a C++20 compiler.

```bash
# Configure (Linux)
cmake --preset linux

# Build
cmake --build build --config Debug

# Run
./build/Debug/prismlauncher
```

Tests use CTest via `ecm_add_test`. Run all:
```bash
ctest --preset linux
```

Run a single test by name (e.g. `FileSystem`):
```bash
ctest --preset linux -R FileSystem
```

Format changed files before committing:
```bash
clang-format -i <file>
```

## Code Style

Chromium-based clang-format (`.clang-format`), C++20, Qt6. Key conventions from CONTRIBUTING.md:

- Types/classes: `PascalCase`
- Private/protected data members: `m_camelCase`
- Private/protected static data members: `s_camelCase`
- Public static const / macros / `const` globals: `SCREAMING_SNAKE_CASE`
- Functions and non-const globals: `camelCase`
- Enum constants: `PascalCase`
- Avoid `[[nodiscard]]` on plain getters

Run `clang-tidy` to check violations (config in `.clang-tidy`).

## Commit Requirements

All commits must be signed-off (`git commit -s`). AI-assisted commits must include:
```
Assisted-by: Claude:claude-sonnet-4-6
```
AI agents **must not** add `Signed-off-by` tags — only the human submitter can legally certify the DCO.

## Architecture

### Upstream Prism Structure
- `launcher/` — all C++ application code (the bulk of the codebase)
  - `launcher/Application.cpp/.h` — main app singleton
  - `launcher/ui/` — Qt widgets and dialogs
  - `launcher/modplatform/` — platform integrations (Modrinth, CurseForge, etc.)
  - `launcher/minecraft/` — Minecraft-specific logic
  - `launcher/tasks/` — async Task framework (used everywhere for network/IO ops)
- `libraries/` — third-party vendored libs (LocalPeer, libnbtplusplus, murmur2, qdcss, rainbow, javacheck, Java launcher)
- `tests/` — CTest unit tests (one `.cpp` per test, linked against `Launcher_logic`)
- `buildconfig/` — build-time config/branding files
- `program_info/` — desktop integration files, icons

### LCSV-Specific Code (in-progress)
- `launcher/modplatform/lcsv/` — pack index structs and parsing
  - `LCSVPackIndex.h/.cpp` — `LCSV::IndexedPack` struct, `loadIndexFromBytes()` parsing `distribution.json`
- `launcher/ui/pages/modplatform/lcsv/` — "LCSV Servers" tab in Add Instance dialog
  - `LCSVListModel` — `QAbstractListModel` backed by `distribution.json` fetch
  - `LCSVPage` — `ModpackProviderBasePage` subclass wired to `NewInstanceDialog`
- **Not yet in CMakeLists.txt** — the LCSV source files exist but aren't wired into the build yet (next step per `LCSV_PLAN.md`)

### Task System
Async work uses `Task` (in `launcher/tasks/`). Network fetches use `Net::NetJob`. Pattern: construct task, connect signals (`succeeded`, `failed`), call `start()`. The update check on LCSV instances will hook into the pre-launch sequence via `BaseInstance`.

### Distribution System
`distribution.json` at `https://mc.lcsv.com.br/distribution.json` lists packs. Each pack has a `packUrl` pointing to an `.mrpack`. Install delegates to `ModrinthCreationTask` (already in upstream Prism). Per-instance metadata goes in `.lcsv-meta.json` in the instance root.
