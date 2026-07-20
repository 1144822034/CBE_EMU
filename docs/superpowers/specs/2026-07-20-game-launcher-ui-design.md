# Game Launcher UI — Design Spec

**Date:** 2026-07-20
**Status:** Draft — awaiting user approval

## 1. Goal

Replace the hardcoded `LOAD_CBE_PATH` chain in `src/main.c` with a runtime **Game Center** screen that displays all available `.CBE` files as icon tiles. The user taps an icon to launch the selected game. The emulator continues running the CBE normally; the launcher is a host-side SDL overlay that appears before any CBE loads and can re-enter between games.

## 2. Scope

### In scope

- Scan `bin/CBE/` for `*.CBE` files at startup.
- Render a scrollable grid of text-based icon tiles, each showing the full game name.
- Support mouse click selection and keyboard arrow + Enter navigation.
- On selection: stop current VM (if running), tear down Unicorn state, reload the chosen `.CBE`, and start a new VM thread.
- Remove the `LOAD_CBE_PATH` `#define` chain from `main.c`; replace with `--cbe=...` CLI override or auto-launch last selected game.
- Add `src/gameLauncher.c` / `src/gameLauncher.h`.
- Update `Makefile` to compile the new unit.

### Out of scope

- No image/icon asset pipeline (text-only tiles).
- No changes to CBE internal code or VM globals.
- No network / mock-service integration.
- No Linux headless-server path (launcher is Windows/SDL only).

## 3. Architecture

### 3.1 New module: `gameLauncher`

A self-contained SDL-overlay module with these responsibilities:

| Function | Purpose |
|---|---|
| `vm_game_launcher_init()` | Scan `CBE/` directory, populate `GameEntry[]`, compute layout. |
| `vm_game_launcher_destroy()` | Free entries. |
| `vm_game_launcher_render()` | Draw background, header, icon grid, scrollbar, selected highlight. |
| `vm_game_launcher_handle_mouse(x, y, button)` | Detect tile clicks and scrollbar drags. |
| `vm_game_launcher_handle_key(key)` | Arrow-key + Enter navigation. |
| `vm_game_launcher_is_active()` | State query for the main loop. |
| `vm_game_launcher_launch(index)` | Stop VM, reload CBE, switch UI state. |

### 3.2 Data model

```c
typedef struct {
    char filename[260];   // e.g. "CBE/江湖OL.CBE"
    char display_name[128]; // extracted short name for tile label
    uint32_t file_size;
} GameEntry;

typedef struct {
    GameEntry *entries;
    int count;
    int selected_index;
    float scroll_offset;     // pixel offset for vertical scrolling
    int columns;
    int rows_visible;
    int tile_w, tile_h;
    int padding;
} GameLauncherState;
```

### 3.3 State machine

The existing `main.c` loop gains one top-level state:

```
UI_GAME_LAUNCHER → user selects → UI_LOADING → VM starts → UI_CBE_RUNNING
UI_CBE_RUNNING   → user presses Esc/back → UI_GAME_LAUNCHER
```

A global flag `g_gameLauncherActive` (or extension of existing `g_uiState`) controls whether the SDL event loop dispatches input to the launcher or to the VM.

### 3.4 CBE reload pipeline

`vm_game_launcher_launch(index)` performs:

1. Set `g_uiState = UI_LOADING`.
2. `pthread_cancel(EmuThread)` and `pthread_join` to stop the running ARM thread.
3. `uc_close(MTK)` to release Unicorn engine.
4. Read the new `.CBE` file into a buffer.
5. `parseCbeHeader()` → `vm_config_program_mapping()`.
6. `uc_open()` with correct endian mode.
7. `uc_mem_map_ptr()` for ROM, stack, manager table, hook table, malloc pool.
8. `uc_mem_write()` for code and BSS/data segments.
9. `vm_initManagerTable()`, set `Global_R9`.
10. `pthread_create(&EmuThread, NULL, RunArmProgram, ...)`.
11. Set `g_gameLauncherActive = false`.

This reuses the exact same initialization sequence already present in `main()` around lines 7884–8001. The refactor extracts that sequence into a reusable function `vm_cbe_load_and_start(path)`.

### 3.5 Layout

- **Tile size:** 120×80 px (width × height), comfortable for full Chinese game names.
- **Columns:** floor((screen_width - margins) / (tile_w + gap)), min 2, max 4.
- **Rows visible:** floor((screen_height - header_h - footer_h) / (tile_h + gap)).
- **Grid origin:** (16, 80) — leaves room for a title bar.
- **Scrollbar:** right edge, 12 px wide, proportional to visible/total.
- **Selected highlight:** bright border + slightly lighter background.
- **Font:** use existing `font_gb.uc3` via `fontEngine.c` at 14–16 px.

### 3.6 Name extraction

From a path like `CBE/江湖OL.CBE`:
1. Strip directory prefix → `江湖OL.CBE`.
2. Strip `.CBE` suffix → `江湖OL`.
3. Use the remainder as the tile label.
4. If the name exceeds tile width, truncate with ellipsis but keep full name in tooltip/status line.

### 3.7 Persistence

- Save the last launched game index to a small JSON/text file under `bin/` (e.g. `bin/game_launcher_last.txt`).
- On next startup, if `--cbe=` is not provided, auto-launch the last selected game and skip the launcher.
- This preserves the existing "starts directly into Jianghu OL" convenience.

### 3.8 CLI overrides

| Flag | Behavior |
|---|---|
| `--cbe=CBE/xxx.CBE` | Skip launcher, load specified CBE directly. |
| `--no-launcher` | Skip launcher, use default or last-selected CBE. |
| `--launcher` | Always show launcher even if last-selected exists. |
| (none) | Auto-launch last-selected if available; otherwise show launcher. |

## 4. Files to modify / create

| File | Action | Notes |
|---|---|---|
| `src/gameLauncher.h` | **Create** | Public API. |
| `src/gameLauncher.c` | **Create** | Implementation (~500–700 LOC). |
| `src/main.c` | **Modify** | Remove `LOAD_CBE_PATH` chain; extract CBE load pipeline; integrate launcher state into event/render loop; add CLI flag parsing. |
| `src/main.h` | **Modify** | Declare `g_gameLauncherActive`, launcher functions. |
| `Makefile` | **Modify** | Add `src/gameLauncher.c` to sources and `obj/gameLauncher.obj` to objects. |
| `docs/re/2026-07-20-game-launcher-ui.md` | **Create** | RE note documenting the host-side launcher design. |

## 5. Risk & mitigation

| Risk | Mitigation |
|---|---|
| VM teardown / re-init not fully clean | Reuse the exact same init sequence as `main()`; add asserts on `uc_*` return codes. |
| pthread_cancel may leave locks held | Ensure `RunArmProgram` checks cancellation points; hold no Unicorn locks across long calls. |
| Large CBE files cause long reloads | Show a loading overlay with progress text during reload. |
| Chinese font rendering issues | Use existing `fontEngine.c` which already handles GBK/UCS2. |
| Scroll position lost on reload | Store scroll offset in persistent config. |

## 6. Testing plan

1. **Unit:** `vm_game_launcher_init()` correctly enumerates all 21 CBE files from `bin/CBE/`.
2. **Integration:** Clicking an icon stops the current VM and starts the selected CBE without crashes.
3. **Navigation:** Arrow keys + Enter work; scroll wheel / scrollbar drag works.
4. **CLI:** `--cbe=...` skips launcher; `--launcher` forces it.
5. **Persistence:** Last-selected CBE auto-launches on next run.
6. **Regression:** Existing Jianghu OL mock-service flow still works unchanged.

## 7. Open questions

None at this time. All user preferences have been captured:

- Host-side SDL overlay (方案 A).
- Text-only icons with full game name displayed.
- Vertical scrolling supported.
