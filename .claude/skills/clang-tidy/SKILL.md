---
name: clang-tidy
description: >-
  Enable one clang-tidy check in the blocking tidy CI job.
  Use when introducing a mechanically enforceable C++ rule.
allowed-tools: Bash Read Grep Glob Edit
---

# Add a clang-tidy check

Grow the blocking tidy job **one check at a time**. Do not turn this
into a style-guide dump or a clang-format gate.

Background: [CONTRIBUTING.md](../../../CONTRIBUTING.md),
[`.clang-tidy`](../../../.clang-tidy),
[`bin/lint.sh`](../../../bin/lint.sh).

## This repository

- First-party TUs and headers: `src/`
- `src/rum/` stays out unless `RUM=ON`
- Run: `NGINX_VERSION=<version> make lint` (Alpine container;
  never a host `compile_commands.json`)
- CI: GitLab job `lint` runs the same script

Leave `tools/`, tests, `dd-trace-cpp/`, `libddwaf/`, and other vendored
trees out unless the user explicitly expands the path filter.

The nginx log-format plugin is a second pass in `bin/lint.sh`. Do not
fold new stock checks into that plugin unless the user asks.

## Hard constraints

- Enable **one** check per change. Prefer uncommenting a deferred line
  in `.clang-tidy` over adding a new check name.
- Do not enable a check group (`bugprone-*`, `readability-*`, …).
- Keep `WarningsAsErrors: '*'`.
- Do not add `CXX_CLANG_TIDY` / `CMAKE_CXX_CLANG_TIDY` to CMake.
- Do not pass `-extra-arg` to the stock tidy pass. The compile database
  from the pinned clang19 configure is the compile line.
- Do not run tidy on the host. `bin/lint.sh` re-execs in Docker.
- Do not mark `lint` `allow_failure`. The job is meant to block.
- Do not widen `-header-filter` or the `src/` TU regex unless asked.
- Keep the shared check list comment at the top of `.clang-tidy` accurate
  if you change enabled or deferred checks.

## Workflow

1. Read `.clang-tidy`. If the rule is already listed under
   “Deferred until existing findings are cleaned up”, uncomment that
   one line and add it to `Checks`.
2. Otherwise add **one** check name to `Checks` (after `-*`).
3. Run `NGINX_VERSION=<version> make lint` from the repo root
   (use the version CI uses if unsure).
4. If it fails:
   - Mechanical first-party fixes in `src/` (not `src/rum/` unless RUM
     is on) are OK in the same change.
   - Do not edit vendored or generated nginx headers to silence tidy.
   - If the cleanup is large or opinionated, comment the check back out
     (name the failing files) and stop. Ask before a repo-wide rewrite.
5. Keep the check only if `make lint` exits 0.

## Local command

```bash
NGINX_VERSION=<version> make lint
```
