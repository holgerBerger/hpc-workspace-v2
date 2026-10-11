# AGENTS.md — hpc-workspace-v2

> HPC workspace management tool suite v2. Manages temporary scratch directories on HPC clusters with expiration, extensions, and admin controls.

## Essential Commands

| Action | Command |
|--------|---------|
| Configure (debug+tests) | `cmake --preset debug` |
| Build (debug) | `cmake --build --preset debug -j` |
| Build (release) | `cmake --build --preset release -j` |
| Unit tests | `ctest --preset debug` |
| Bats CLI tests (non-root) | `bats bats/test --filter-tags '!sudo'` |
| Bats CLI tests (root) | `sudo bats bats/test --filter-tags sudo` |
| Format | `cmake --build --preset format` |

All commands use CMake presets (`--preset`). The `debug` preset also enables ASAN/UBSAN/leak sanitizers.

## Project Structure

```
src/           — shared lib (ws_common) + all CLI tools (one .cpp per binary)
tests/         — Catch2 unit tests per module (utils, config, caps, db, etc.)
bats/test/     — bats integration tests (shell-level tests)
man/           — man pages (man1 = user, man8 = admin)
docker/        — Docker images for CI and testing
vm/            — Vagrant files for Rocky/Alma Linux VMs
```

### Key Source Modules

| Module | Purpose |
|--------|---------|
| `ws_common` (static lib) | Shared code: config, db, caps, user, utils |
| `caps.{cpp,h}` | Privilege elevation (libcap capabilities OR SUID) — compile-time + runtime dispatch via `WS_CAPA` macro |
| `config.{cpp,h}` | YAML config parser (Global_config + per-Filesystem_config), validates via `Config::validate()` |
| `db.{h} / dbv1.{cpp,h}` | Database abstraction: `Database` (abstract) and `DBEntry` (abstract) with v1 flat-file implementation |
| `user.{cpp,h}` | User/group helpers (namespace `user::`) |
| `utils.{cpp,h}` | String, file, ACL, rmtree helpers (namespace `utils::`) |
| `mail.{cpp,h}` | Email sending via libcurl (namespace `mail::`) |

### CLI Tools

- **User tools** (installed to BINDIR): `ws_allocate`, `ws_find`, `ws_list`, `ws_register`, `ws_release`, `ws_restore`, `ws_send_ical`, `ws_stat`, `ws_validate_config`
- **Admin tools** (installed to SBINDIR): `ws_expirer`, `ws_editdb`, `ws_prepare`
- **Shell scripts** (BINDIR): `ws_share`, `ws_extend`
- Each tool is a single `.cpp` file in `src/`, links against `ws_common`

## Privilege Model (Critical Gotcha)

The privilege system has **three modes**, selected at compile-time and runtime:

1. **Capabilities mode** (`WS_CAPA` defined, `libcap` found): Tools use Linux capabilities instead of SUID. Checked via `Cap::hasCaps()`.
2. **SUID mode** (no libcap): Binaries are setuid root. Checked via `Cap::isSetuid()`.
3. **User mode** (running as regular user, no elevation): For testing. Checked via `Cap::isUserMode()`.

The `Cap` class wraps all privilege operations. Tools call `raise_cap()` before privileged ops and `drop_caps()` after. Tests use user mode (no SUID needed) — bats tests in `bats/test/` run without root unless tagged `@sudo`.

**Gotcha**: ws_restore has a post-build copy step creating `ws_restore_notest` — the test suite expects the unmodified `ws_restore` binary to exist but calls `ws_restore_notest` to avoid triggering restore-side effects.

## Testing

### Unit Tests (Catch2)

- Located in `tests/`, one file per module (`X_test.cpp`)
- Linked against `ws_common` (and `mail` where needed)
- `utils_test.cpp` defines `CATCH_CONFIG_MAIN` — it's the only one with `main()`
- Tests define global `debugflag`, `traceflag`, `debuglevel` to satisfy `ws_common` links without providing real implementations
- Run via `ctest --preset debug`

### Bats Integration Tests

- In `bats/test/` — one file per CLI tool
- Tests set `PRESET` env var to find binaries under `build/{preset}/bin/`
- Config files live in `bats/ws.conf` and `bats/ws.d/`
- Bats tests in `bats/test_cap/` and `bats/test_setuid/` test capability and SUID modes respectively
- Non-sudo tests run in "user mode" (no privilege elevation)
- Tags: use `--filter-tags '!sudo'` for non-privileged, `--filter-tags sudo` for privileged

## Configuration Format

Config is YAML (yaml-cpp). Key structure:
- `clustername`, `smtphost`, `mail_from`, `admins`, `adminmail` — global settings
- `dbuid`/`dbgid` — UID/GID for database ownership
- `workspaces:` (or `filesystems:`) — per-filesystem sections with `spaces`, `database`, `deleted`, `keeptime`, `duration`, `maxextensions`, ACL lists

The config code depends on **both** yaml-cpp and rapidyaml. A planned migration to rapidyaml-only is currently on hold.

## Dependencies

Two dependency modes via `BUILD_WITH_VCPKG`:

| Mode | Description |
|------|-------------|
| `FetchContent` (default) | CMake fetches yaml-cpp, rapidyaml, fmt, GSL, spdlog, thread-pool, Catch2 at configure time |
| `vcpkg` | All deps from vcpkg; requires `./vcpkg/bootstrap-vcpkg.sh` + vcpkg toolchain |

System deps: Boost (program_options, system), libcurl, libcap (optional), pthreads.

**Gotcha** with vcpkg: `rapidyaml` header path differs — `src/CMakeLists.txt:38-44` has special include-path logic for vcpkg's ryml layout. Also, `ms-gsl` is header-only with no CMake config under vcpkg, so CMake `find_path` is used instead of `find_package`.

## Naming Conventions

- Source files: `ws_*.cpp` for tools, lowercase modules otherwise
- Variables: camelCase in classes, snake_case for CLI option variables
- Classes: PascalCase with descriptive names (`Filesystem_config`, `DBEntry`)
- Headers use `#ifndef FOO_H` include guards (not `#pragma once`)
- Namespace usage: `using namespace std;` at top level; specific namespaces (`utils::`, `user::`, `mail::`) for helpers

## Style

- `.clang-format` — LLVM-based, 120 column limit, 4-space indent, no tabs, attach braces
- Run `cmake --build --preset format` or `cmake --build --preset debug --target clang-format`

## Build Info

`build_info.h` is auto-generated at CMake configure time with version string and git commit hash. The `IS_GIT_REPOSITORY` macro enables git hash capture.
