# Automation Facilities

## Legacy smoke helper

The emulator retains a small SDL-side helper for historical smoke tests.  It
is not suitable for protocol conclusions because its actions are time based.
It now captures the emulator-owned LCD cache as PNG rather than reading the
desktop window.

Run from `bin` so game assets resolve:

```powershell
.\main.exe --autotest --shot-ms=1000 --actions=5000:key:f,17000:key:f,19000:key:q
```

## Options

- `--autotest`: enable screenshots and scripted input.
- `--shot-ms=N`: save one PNG every `N` milliseconds. Minimum is `100`.
- `--max-ms=N`: exit the process after `N` milliseconds. This is intended for smoke tests because the emulator threads do not currently have a graceful stop signal.
- `--actions=...`: comma or semicolon separated action list.

## Action Format

Tap:

```text
time_ms:tap:x:y
```

Key:

```text
time_ms:key:f
time_ms:key:q
time_ms:key:enter
time_ms:key:esc
```

Hold key:

```text
time_ms:hold:w:2000
time_ms:hold:s:2000
time_ms:hold:a:2000
time_ms:hold:d:2000
```

Useful key mapping:

- `f`: OK
- `q`: left soft key
- `e`: right soft key
- `w/s/a/d`: d-pad

## Output

- Screenshots: `bin/autotest/screens/*.png`
- Test state summary: `bin/autotest/state.txt`

The state file is only produced when `--autotest` or `CBE_AUTOTEST` is enabled. It is a test artifact and must not be treated as protocol evidence; protocol conclusions belong in `docs/re/`.

## State-driven scenarios

New regression scenarios use `--automation-scenario=<stable-id>` together with
`--automation-artifacts=<new-run-directory>`.  They do not use SDL window
input or desktop capture.  They observe only PC/packet/screen state, enqueue
normal press/release events, and copy RGB565 pixels from `Lcd_Cache_Buffer`
immediately after `UpdateLcd()`.

The initial scenario is documented in
[`2026-08-03-shop-return-hangup-automation.md`](2026-08-03-shop-return-hangup-automation.md).
