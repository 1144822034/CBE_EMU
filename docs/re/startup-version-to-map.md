# Startup Version To Map

phase: startup version handshake -> title login -> map entry
status: validated

## Request / Response Chain

1. Version request

- request: WT `18/9`, sample len `137`, fields include `version`, `codeVersion`, `client`, `Screen`, `plat`, `imsi`, `proj`.
- response source: `builtin-version`.
- response len: `63`.
- effect: startup leaves "获取版本信息" and reaches title menu.

2. Login and title flow

- request: WT `1/12`, sample len `86`.
- response source: `builtin-login`.
- follow-up requests observed during validation:
  - WT `1/12` -> `builtin-login`
  - WT `1/16` -> `builtin-title-rolelist-wait-server-select`
  - WT `1/4` -> `builtin-title-server-select`
  - WT `1/6` -> `builtin-title-role-select`
  - WT `5/10` -> `builtin-group-type1`
  - WT `7/7` -> `builtin-game-type`
  - WT `12/1` -> `builtin-scene-resource-followup`

## Bug Fixed

`vm_net_mock_build_battle_operate_response()` returned a battle response for non-battle packets because the non-match branch did not return unless the relaxed battle detector matched. The startup version request `WT 18/9` was incorrectly handled as `builtin-battle-operate`, so the startup parser never received version metadata and stayed on the communication screen.

Fix: return `0` whenever the strict battle-operate detector does not match. The relaxed battle path remains handled by `vm_net_mock_build_battle_operate_response_fallback()`.

## Runtime Evidence

Validated with:

```powershell
cd E:\DevOs\CBE_EMU\bin
.\main.exe --autotest --shot-ms=1000 --actions=5000:key:f,17000:key:f,19000:key:q,23000:key:f,29000:key:f,35000:key:f
```

Final screenshot reached the map UI with title `蓬莱仙岛_十二域` and the player label visible.

## Unknowns

- The title/login chain still emits repeated queued event entries while callbacks are pending. Current behavior is parser-safe and reaches map, but the scheduler duplicate policy may need a separate cleanup pass.
- The role-list wait response is still staged as a no-op before explicit server select; keep it unless IDA evidence proves the live server combines those payloads.

## 2026-06-30 No-Account Login Server List

Runtime negative evidence:

```text
net_send connect=2 wt=1/12 len=86 source=builtin-login resp=141
net_send connect=2 wt=1/12 len=86 source=builtin-login resp=23
net_send connect=2 wt=1/16 len=9 source=builtin-title-rolelist-wait-server-select resp=23
```

The no-account title button sends subtype `1/1/12` with empty username and
password. This first visible request should enter the server-list screen. Two
bad response shapes were observed:

- subtype-12 `result=1` without `serverinfo/servernum/color` moves the UI into a
  list state with no rows to draw;
- subtype-12 `result=4` with server info parses the list bytes but then enters
  the `sub_10C6(..., sub_5922)` prompt/callback path, which can leave the title
  screen resending the same `1/12` request.

Fix:

- generic subtype `1/1/12` server-list fallback stays on subtype-12 `result=1`
  with `serverinfo`, `color`, `servernum`, and `newVer`;
- for the true first-run no-account case where the request carries empty saved
  credentials, the mock-service may override that with subtype-12 `result=3`
  plus `information/username/password`, so the client can persist the issued
  guest account and reuse it on later `1/12` requests.

IDA evidence:

- `mmTitleMstarWqvga.cbm:net_handle_login_response(0x16DC)` parses
  `serverinfo/servernum/newVer` for result `'1'` when the stage flag is not `1`;
- `login_alt_result_dispatch(0x19C2)` sends result `'1'` directly to
  `login_stage_success_dispatch(0x1960)`, whose stageFlag `4` target is the
  title list target;
- result `'4'` goes through `sub_10C6(..., sub_5922)` before success dispatch.

## 2026-06-29 Update-Chunk Guard

Resource update chunks are confirmed as WT `18/7` requests with
`name/screen/type/start/version` fields (`send_update_chunk_request`
`0x01036D80`). The old dispatch guard only searched for `start` plus `id` or
`name`, which can accidentally consume scene packets such as WT `2/3` when
their payload happens to contain those strings.

`builtin-update-chunk` is now gated by WT kind/subtype `18/7` plus the existing
`start` and `id/name` field evidence.

## 2026-06-29 Clean Resource Release Version Loop

> Superseded for server publishing on 2026-07-19: startup module availability
> is now controlled by `server_update_catalog.tsv`, not inferred from files
> installed in the local client resource directory. See
> `2026-07-19-game-content-update-workflow.md`. The evidence and narrow WT
> `18/6` dispatch described below remain valid.

After clearing the released resource directory, the client can recreate
`JHOnlineData/mmGameMstarWqvga.cbm` and `JHOnlineData/mmorpg_updateversioncbm`
without creating `JHOnlineData/MMORPGTempcbm`. Treating the temp network-update
name as mandatory makes the server answer `WT 18/5` with `result=1`, then the
client repeatedly calls `send_update_chunk_request(2)` and emits `WT 18/6`.

IDA evidence:

- `handle_version_update_response` at `0x01037473` handles kind `18`. For
  subtype `5`, `result & 1` enters the update-chunk path; otherwise it marks
  version negotiation complete.
- `send_update_chunk_request` at `0x01036D80` sends subtype `6` for the main
  update payload and subtype `7` for named resource chunks.
- `handle_update_chunk_response` reads subtype `6` as
  `totalsize/crc/version/data`; subtype `7` additionally reads `type/name`.

Fix:

- Installed-resource detection now accepts the released `mmGameMstarWqvga.cbm`
  when `mmorpg_updateversioncbm` exists, instead of requiring
  `MMORPGTempcbm`.
- `WT 18/6` is handled before the generic version handler and returns a narrow
  subtype `6` chunk response. This prevents the generic `version` detector from
  answering a chunk request with another subtype `5` response.

## 2026-06-30 Named Resource Update Cache Pollution

Runtime failure:

```text
mock_update_chunk subtype=7 start=45056 chunk=3802 total=48858 source=00_*.sce
vm_malloc FAILED size=4278124288
```

The crash happened after the update screen because `WT 18/7` requested a named
scene resource, but the mock could read the client's writable
`JHOnlineData/<name>` cache first. A previous bad run had written
`mmGameMstarWqvga.cbm` bytes into `JHOnlineData/00_蓬莱仙岛01.sce`; the file was
48858 bytes and had the same hash as the main CBM. The client accepted the
`name` field and wrote the payload as `.sce`, then later loaded it as scene
data and read a bogus large allocation size.

IDA evidence:

- `handle_update_chunk_response` at `0x010372D6` reads subtype `7` fields
  `totalsize`, `crc`, `version`, `type`, `name`, and `data`.
- It copies the response `name` into a local buffer, accumulates a signed-byte
  checksum over `data`, and writes the completed named resource through
  `WriteResBinToTempFile(name, buffer)`.

Fix:

- Named update chunks now prefer clean server resources under
  `../web/fs/JHOnlineData/<name>` or `web/fs/JHOnlineData/<name>` before falling
  back to the writable client cache. The `../web/fs` form is required when the
  emulator is launched from `bin/`.
- Named update chunks no longer fall back to wrapping the main CBM as a missing
  resource. If the named resource is missing, the `18/7` handler records a
  `mock_update_chunk_missing` line and does not fall through to the generic
  version handler.
- `mock_update_chunk` logs convert the GBK request name to UTF-8 and include the
  clean source path:

```text
mock_update_chunk subtype=7 file=00_蓬莱仙岛01.sce start=0 chunk=279 total=279 ... path=web/fs/JHOnlineData/00_蓬莱仙岛01.sce
```

2026-06-30 follow-up: after teleporting into `c04临安府_03`, the client repeatedly
entered the update screen and then crashed in `SetMapAndClampViewport(0x0100575C)`
from `DrawSceneMapLayer(0x0104676C)`. Local evidence showed the writable cache
had polluted files:

```text
web/fs/JHOnlineData/c04临安府_03.sce 397 bytes
bin/JHOnlineData/c04临安府_03.sce    469 bytes
web/fs/JHOnlineData/04临安府_03.map  874 bytes
bin/JHOnlineData/04临安府_03.map     508 bytes
```

If `WT 18/7` runs from `bin/` and cannot find `../web/fs`, it can fall back to the
polluted `JHOnlineData` cache and re-serve the bad map/SCE bytes. The update
source search order now covers both working directories before the cache fallback.

Validation:

- Temporarily removed the polluted `bin/JHOnlineData/00_蓬莱仙岛01.sce`.
- Runtime downloaded `00_蓬莱仙岛01.sce`, `00_蓬莱仙岛01.map`, and
  `m_palace2.gif` from `web/fs/JHOnlineData`.
- The recreated `bin/JHOnlineData/00_蓬莱仙岛01.sce` is 279 bytes and the game
  reached the map screen.

