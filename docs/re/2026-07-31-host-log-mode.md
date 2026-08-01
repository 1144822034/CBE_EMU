# Host log mode (debug vs production)

Date: 2026-07-31

## Problem

Tagged `printf` traffic (`[info]` / `[debug]` / …) on the mock service and emulator client was always-on and expensive on hot network/UI paths.

## Change

Central filter in `src/cbe_log.c` (Android copy under `JianghuOL/.../cbeEmu/`).

| Mode | Emits |
|------|--------|
| `production` (default) | `[warn]`, `[error]` |
| `debug` | all levels + untagged |
| `quiet` | `[error]` only |
| `off` | nothing |

Configure:

- Env: `CBE_LOG_MODE=debug|production|quiet|off` (or `CBE_LOG_LEVEL`)
- CLI: `--log-mode=debug`, `--debug-log`, `--quiet-log`, `--no-log`
- Android: `BuildConfig.DEBUG` → debug, release → production; override with SharedPreferences `cbe_runtime` / `log_mode`

Startup always prints one unfiltered line: `log_mode=...`.

## Verify

1. `make -j2`
2. Run server without flags → only warn/error (+ the mode banner)
3. `CBE_LOG_MODE=debug bin/jh-online-server.exe ...` → full `[info]` traffic returns
