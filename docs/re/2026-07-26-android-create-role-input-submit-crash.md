# 2026-07-26 Android Create-Role Input Submit Crash

## Symptom

On Android, title create-role opens the host name dialog. After typing a Chinese
role name and tapping 确定, the process aborts (`SIGABRT` / `Fatal signal 6`).

## Contract

- Guest opens CoolBar text input (`vm_input_open`) with a UCS-2 edit buffer and
  finish callback (create-role uses title CBM `login_form_open_editor(4)` /
  `sub_5A30`).
- Host must deliver committed UCS-2 text, then invoke the callback on the
  emulator thread via `VM_EVENT_INPUT_DONE` / `vm_input_finish`.
- Unicorn `uc_mem_read` / `uc_mem_write` and nested `uc_emu_start` are only safe
  on the emulator thread.
- Android UI polls `getPrintBuffer()` and builds a Java `String` from native
  bytes; those bytes must be Modified UTF-8 safe for JNI / ART.

## First Deviation (abort observed in `tmp-android-crash.log`)

Submit itself completed on the emu path (`android_submit` → callback → lcd40).
Debug watch then printed guest GBK role text into the Android print buffer
(`srcText='˛ĚÎÄź§'` / illegal UTF-8 start byte `0xb2`).

UI handler called `MainActivity.getPrintBuffer()` → `NewStringUTF` on that
buffer → ART:

`JNI DETECTED ERROR IN APPLICATION: input is not valid Modified UTF-8: illegal start byte 0xb2`

That is the first abort: log-bridge charset contract, not guest input callback
failure.

Earlier investigation also fixed a separate UI-thread Unicorn race: confirm used
to `uc_mem_write` from the Java thread while `RunArmProgram` could still be in
`uc_emu_start`.

## Fix

1. **Input submit (emu-thread apply):** UI thread only copies Java UTF-16 into a
   host pending buffer and sets `g_vmAndroidInputPendingReady`. Emulator thread
   (`INPUT_DONE` / `scheduler_tick`) applies UCS-2 to scratch + guest target and
   calls `vm_input_finish`.
2. **Print buffer JNI:** `getPrintBuffer` builds `new String(bytes, "UTF-8")`
   instead of raw `NewStringUTF`, so malformed sequences are replaced instead of
   aborting ART.
3. **Debug hygiene:** `lcd40` watch `printf` converts GBK guest strings to UTF-8
   before writing into the Android print buffer.

## Verification

- Rebuild `libmtksim.so` / APK.
- Title → create role → tap name → enter Chinese/ASCII name → 确定.
- Expect `android_submit cancel=0 len=N ...` and return to create-role form with
  the name shown; no `NewStringUTF` / `Fatal signal 6` when the print buffer
  contains Chinese debug lines.
- Cancel path should close the dialog without crashing.
