# Runtime Contracts

Use this file for client-facing platform behavior that the emulator must honor.

Examples:

- screen stack semantics
- timer behavior
- file I/O expectations
- VM callback/event ordering
- socket lifecycle behavior
- text/input/lcd quirks

Each entry should describe the contract and how it was observed.

## Network Mocking Principles

- let the game code advance itself; the emulator should only implement platform/API semantics and server-like network responses
- do not mutate CBE globals directly from the network mock
- do not force CBE function return values to push progress
- temporary read-only logging is acceptable while validating a path, but remove hooks that change game state after the behavior is confirmed
- when manually validating startup/login flows, launch `bin/main.exe` with `E:\DevOs\CBE_EMU\bin` as the working directory and drive clicks manually

## Screen Stack Removal/Resume

Confirmed screen-manager contract:

- when the update screen calls screen-manager idx `6` to remove itself, the emulator must resume the new top-of-stack screen underneath it
- this is required for the startup/update flow to continue naturally after the update/version callback chain finishes

## Dynamic CBM Module Context

Confirmed contract for pool/dynamic screens:

- when a dynamic CBM screen calls `vmAddScreen` from pool code, the emulator must preserve the call-time `R9` / module base
- when that screen later becomes active again, the emulator must restore the same module context before calling its entries

Without this, lower-screen resume and dynamic-title/login transitions will run under the wrong module state.

## Main CBE Small-Data Base

Confirmed emulator/loader contract:

- for the main CBE, `R9` is set to the copied module data base, not to a heap-local scene object
- the loader copies code bytes to `ROM_ADDRESS`
- it then copies parser-selected module data bytes from `fileBuffer + g_cbeInfo.BssDataOffset` to `ROM_ADDRESS + g_cbeInfo.headerInt2`
- after that it sets `Global_R9 = ROM_ADDRESS + g_cbeInfo.headerInt2` and writes the same value to guest `R9`

Evidence:

- `src/main.c:9141-9146`
- `src/main.c:9158`
- `src/cbeParser.c:80-124`

Implication:

- fixed `R9+offset` blocks such as the scene actor-asset ops descriptor at `R9+0x5C48` may be image-seeded or loader-relocated small-data
- they should not be assumed to belong to heap-local scene object constructors like `scene_object_vtable_init(*[R9+0x54AC])`

## Nullable Screen Entrypoints

Confirmed screen lifecycle behavior:

- `init`
- `resourceLoad`
- `destroy`

may legitimately be `0` in a screen function table.

The emulator must treat null screen entries as no-op callbacks and skip them without failing the lifecycle.

## Pool Screen Idle Logic

Current contract is still intentionally conservative:

- the simulator should not synthesize idle/inputless logic events for pool screens yet
- the no-input logic tick semantics for pool screens are still unconfirmed

Future work should confirm from logs whether the CBE or firmware provides a dedicated idle event path for those screens.

## Game Utility Geometry Helpers

Confirmed by firmware static analysis (`8533n_7835.axf`):

- `CdRectPoint(left, top, right, bottom, x, y)` returns true when `x` and `y` are inside the rectangle using inclusive bounds
- concrete predicate: `right >= x && left <= x && bottom >= y && top <= y`
- the emulator should implement the same contract for both the newer `vm_manager_game_util` slot `idx=2` and the legacy `vm_manager_gameold` slot `idx=51`

## UCS-2 File Paths

Confirmed runtime/platform contract:

- CBE file APIs may pass ASCII resource names inside UCS-2LE path buffers
- valid ASCII filename characters include `+`, as seen in `./\JHOnlineData\+num.gif`
- the emulator must detect that buffer as UCS-2LE and convert it before host path normalization

Evidence:

- after entering the first map portal, piclib requested `+num.gif`; raw memory held the complete UCS-2LE path, but the earlier detector rejected `+` and decoded only `.`
- that misclassification returned a pseudo-directory handle instead of opening `JHOnlineData/+num.gif`
