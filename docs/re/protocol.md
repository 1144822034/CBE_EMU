# Protocol Notes

This file is the canonical packet/protocol summary for the project.

Status tags used in this file:

- `confirmed`: established by code inspection, xrefs, logs, memory observation, or repeatable runtime behavior
- `hypothesis`: plausible inference that still needs direct confirmation

When adding new findings here:

- separate transport framing from message semantics
- record request triggers and response side effects
- mark each important claim as `confirmed` or `hypothesis`
- include enough detail for future server implementation

## WT Packet Framing

Current mock/server-side packet construction uses the `WT` container.

Status: `confirmed`

Framing:

- `out[0..1] = "WT"`
- `out[2..3] = packet_len` in big-endian order
- `out[4] = object_count`
- each object header is 6 bytes:
  - `major`
  - `kind`
  - `subtype`
  - `0`
  - `object_len_be16`

Field encoding inside an object:

- field name is encoded as `name_len, name`
- field value is encoded as `value_len_be16, value`
- integer field values are internally stored as `0, byte_len, big_endian_value`
- string/blob field values are internally stored as `data_len_be16, data`

Implementation helpers already exist in `src/main.c` and should be reused instead of hand-building packet headers:

- `vm_net_mock_begin_wt_object`
- `vm_net_mock_finish_wt_object`
- `vm_net_mock_finish_wt_packet`
- `vm_net_mock_put_object_u8`
- `vm_net_mock_put_object_u32`
- `vm_net_mock_put_object_blob`
- `vm_net_mock_put_object_string`

## Startup Update Protocol

### Version Check Response

Trigger request contains `version`.

Status: `confirmed`

Correct response shape contains two objects:

- subtype `5`
  - field: `result`
- subtype `9`
  - fields: `type`, `id`, `code`

Why this is required:

- `handle_version_update_response` consumes subtype `5` field `result`
- `startup_update_net_callback` consumes subtype `9` and enters `startup_handle_update_metadata`
- subtype `5` alone can drive `update_state=2`, but the startup screen does not remove itself and the UI stalls
- `type=0` is the confirmed "no update / local data already usable" path and the CBE removes the startup screen on its own

When local `JHOnlineData/MMORPGTempcbm` and `JHOnlineData/mmorpg_updateversioncbm` already exist, the built-in version response currently returns:

- `result = 0`
- an additional close event `9` after the data event

Do not change that close event to `8`; the current client net wrapper does not route event `8` into the active object callback.

### Update Chunk Response

Trigger request contains `start` and `id`.

Status: `confirmed`

Response fields:

- `totalsize`
- `crc`
- `type`
- `name`
- `data`

Rules:

- `start` must be echoed from the request field
- `type` should be echoed from the request field when one is present
- `name` should be echoed from the request field when one is present; hardcoding `MMORPGTempcbm` is only correct for the generic startup/update package path
- `crc` is the signed-byte running sum up to the end of the current chunk
- chunk size is currently limited to `0x1000`

Current payload source priority:

- exact local `JHOnlineData/<request.name>` when the request carries a non-empty resource name
- mock resource wrappers for the known synthetic names `c_mock_missing_motion.actor`, `mock_missing_motion.actor`, and `mock_actor_image.gif`
- `JHOnlineData/MMORPGTempcbm.mock`
- `JHOnlineData/mmBattleMstarWqvga.cbm`
- `JHOnlineData/mmGameMstarWqvga.cbm`
- `JHOnlineData/mmTitleMstarWqvga.cbm`

Do not reuse the already-installed `JHOnlineData/MMORPGTempcbm` as the download source, or the true install/update path will be masked.

Additional confirmed post-scene branch:

- after the first-scene HUD divide-by-zero is bypassed, the client can enter a later `18/7` resource-update loop whose request key is not `MMORPGTempcbm`
- the latest clean logs show repeated `trace_update_request_prepare ... name=侠剑江湖 type=1 start=0/4096/...` while the UI displays `"正在更新资源文件, 请稍候!"`
- when the mock incorrectly replies with `name=MMORPGTempcbm`, storage logs show the temp file being renamed to `JHOnlineData/MMORPGTempcbm` and the client then immediately failing another local open for `JHOnlineData/侠剑江湖`
- therefore this branch requires preserving the requested local resource key in the `18/7` response, even if the payload bytes are still coming from a fallback mock source
- the next rerun confirms that first wire-level fix: response `1/18/7` now echoes the request's non-ASCII `name` bytes and `type=1`
- however, the same run also proves a second contract boundary exists below the protocol layer: after accepting those echoed bytes, the client forwards the same resource key into local file rename/open calls, so the emulator's file shim must decode guest path bytes consistently with the network payload encoding or the update still stalls on a host-side garbled filename
- the latest rerun confirms that the path bridge is now good enough for the first reopen: storage trace shows the temp file rename succeeds, the immediate reopen succeeds, and the very first read returns `hex=fefefefe`
- static IDA for `sub_100D338()` (`0x0100D338`) explains the remaining mismatch: this local resource-open path expects the downloaded file to begin with a 4-byte little-endian payload length before the real body
- therefore, when `18/7(type=1,name=<non-empty>)` falls back to a generic payload instead of an exact local `JHOnlineData/<name>` file, the fallback must be wrapped as `u32le payloadLen + payload`; sending raw CBM bytes makes the reopen interpret `fefefefe` as the length field and remain stuck in the update flow
- the next rerun exposes one more cache-layer consequence of that same bad download: once `JHOnlineData/侠剑江湖` already exists on disk with the old `fefefefe` header, the client may reopen that stale local file immediately on the next scene-enter path before any fresh `18/7` request is sent
- the emulator therefore needs a paired local-cache guard in addition to the network wrapper: extensionless non-ASCII `JHOnlineData/<name>` files opened read-only must be rejected when their first `u32le` header is not a plausible `payloadLen <= fileSize-4`, and the mock server must likewise ignore the same bad file as an exact payload source for later named `18/7` replies

### Verified Startup Chain

Status: `confirmed`

Startup/update path:

1. startup screen state advances to `7`
2. net open event `5` triggers `send_version_update_request`
3. mock returns subtype `5 + 9` version response
4. `handle_version_update_response` sets update flags and `update_state=2`
5. `startup_handle_update_metadata` parses `type/id/code`
6. the CBE removes the startup screen on its own
7. the emulator resumes the lower screen
8. the client loads `JHOnlineData/mmTitleMstarWqvga.cbm` and enters the dynamic CBM screen

## Post-Scene `0x1B` Chain

### `0x1B/12` field `name`

Status: `confirmed`

`kind=0x1B subtype=12` is handled by `net_handle_fb_target_dispatch()` at `0x01010BC0`.

Confirmed flow:

- subtype `12` reads field `result`
- only when `result == 1`, it calls `ui_apply_named_posinfo_target_with_fb()`
- that wrapper stores field `fb` into active UI state and then forwards into `ui_apply_named_posinfo_target()`
- `ui_apply_named_posinfo_target()` reads server fields `name` and optional `posinfo`, copies `name` into active UI object state, and passes `name + posinfo` into UI manager method `+116`

This establishes that `0x1B/12.name` is a named-target / display-location semantic, not a resource filename semantic.

Additional confirmed request/response pairing:

- the current synthetic `0x1B/12` is only appended inside `mock_scene_change_combo_response()` when the incoming post-enter composite WT request contains object `1/0x1B/11`
- the triggering request is the observed `len=86` packet sent immediately after `trace_scene_send_map_enter_request`, with object sequence:
  - `1/0x0C/1`
  - `1/7/42`
  - `1/2/3(maptype,mapID,exitID)`
  - `1/0x1B/11`
  - `1/0x0C/1`
  - `1/7/42`
- this means the live `0x1B/12` should be treated as the server-side answer to the embedded `0x1B/11` subrequest inside the scene-enter follow-up packet, not as a direct answer to the `scene+posinfo` object itself

Additional confirmed buffer alias:

- `scene_send_map_enter_request()` (`0x0100F9B4`) serializes outgoing request field `mapID` from `*(_DWORD *)(R9+21676) + 1141`
- `ui_apply_named_posinfo_target()` (`0x0100E9B8`) writes incoming server field `name` from `0x1B/12` into that same `*(_DWORD *)(R9+21676) + 1141` buffer

This strongly suggests the correct `0x1B/12.name` is some map/scene/location text compatible with later `currentMapIdText` / `mapID` flow, not an actor or resource filename.

Additional confirmed producer / consumer chain for `uiState + 1141`:

- explicit writers:
  - `ui_apply_named_posinfo_target()` (`0x0100E9B8`) clears `uiState+1141` and copies incoming server field `name`
  - `scene_rebuild_runtime_nodes()` (`0x0100F7A6`) also clears `uiState+1141` and copies its `currentMapIdText` argument there, but only when its first argument `varg_r0_1` is nonzero
  - `net_handle_actor_move_info()` (`0x01012D9A`) case `12` also clears `uiState+1141` and copies incoming business field `name` there
- explicit readers / reusers:
  - `scene_send_map_enter_request()` (`0x0100F9B4`) reads `uiState+1141` as outgoing WT field `mapID`
  - `scene_rebuild_runtime_nodes()` reuses the same `currentMapIdText` string as the stacked extra argument into `sub_10352AE()`, which is the confirmed upstream source of the later `parse_actor_motion_descriptor()` misroute

Scene-enter branch behavior is now narrower:

- fresh scene-enter path inside `scene_runtime_init_and_sync()` (`0x010132A8 -> 0x01013586`) prepares a temporary text buffer at `R9+0x5E46`, validates it via `sub_100EEBC()` / `sub_100FA40()`, and then calls `scene_rebuild_runtime_nodes(varg_r0_1=1, ..., currentMapIdText=R9+0x5E46)`
- pending scene-switch / restore path inside the same function (`0x010131C4 -> 0x010131FE`) does **not** overwrite `uiState+1141`; instead it validates the existing `uiState+1141` contents with `sub_100FA40(uiState+1141)` and then calls `scene_rebuild_runtime_nodes(varg_r0_1=0, ...)`, which skips the internal copy into `uiState+1141`

This means the buffer is expected to persist as a scene/map text slot across restore/re-enter transitions. `0x1B/12.name` can therefore corrupt later map-enter and scene-rebuild behavior specifically because it overwrites a buffer that the restore path expects to remain a valid current-map/current-location text value.

Additional validator evidence for the fresh-enter text slot:

- `sub_100EEBC()` (`0x0100EEBC`), used by both the fresh-enter temporary buffer and the restore-path `uiState+1141` buffer, returns success when either:
  - a manager compare callback at `R9+0x4DA0 -> +0x48` accepts the input, or
  - the first byte of the input string is `'c'` (`0x63`)
- `sub_100FA40()` is the wrapper used at both callsites in `scene_runtime_init_and_sync()`
- runtime evidence already shows outgoing `mapID` strings such as `c00PenglaiXiandao_01`

This makes “internal scene/map identifier string” a stronger fit for the shared `+1141` / `R9+0x5E46` text slots than a natural-language map display name. It does not yet prove that every valid value must be `c...`, because the compare callback may also accept other forms, but the observed post-login path is now highly consistent with a c-prefixed scene id.

Related confirmed fallback behavior:

- `sub_100EEE0()` does **not** write `uiState+1141` itself
- instead, it first validates the current `uiState+1141` contents with `sub_100EEBC(uiState+1141)`
- only when that validation fails, it copies its caller-supplied `displayNamePtr` into a separate 4-entry fallback table at `R9+0x5CE4`
- current callers of `sub_100EEE0()` include:
  - `sub_100F094()` actor/scene-node parsing
  - `sub_10159DA()`
  - `scene_parse_npcinfo_and_spawn_npcs()` (`0x01037A9C`)

This reinforces that actor/NPC display names are treated as a fallback/auxiliary label source when the main current-scene text slot is invalid, not as the primary semantic of `uiState+1141`.

Supporting static evidence:

- `ui_apply_named_posinfo_target_with_fb()` at `0x0100EC66`
- `ui_apply_named_posinfo_target()` at `0x0100E9B8`
- the same helper is also reused by `net_handle_guild_hall_enter_response()` at `0x0103C224`, which further supports the interpretation that this is a generic named-target/location helper rather than a file-download API

### Later `type=6` request field `name`

Status: `confirmed`

The repeated `type=6` WT request is built by `send_update_chunk_request()` at `0x01036C66`, not by the startup version-check path.

Confirmed serialized fields there:

- `name`
- `screen`
- `type`
- `start`
- `version`

The startup version-check path is separate:

- `send_version_update_request()` at `0x0103B2D6`
- `handle_version_update_response()` at `0x0103751C`

On current good startup runs, `mock_update_installed yes` proves startup version update is already skipped, so later `type=6` traffic belongs to a different resource/update path.

### Resource-miss to `type=6` bridge

Status: `confirmed`

Local resource-open failure is bridged into the later `type=6` request path through the resource manager:

- `sub_1044DA0()` calls `sub_100D2BE()` to open a local resource by name
- on failure, it records the missing resource name into a net-manager pending-entry table
- `sub_1044EF6()` marks a pending callback slot for that same missing resource
- later `send_update_chunk_request()` consumes pending entries through `sub_1036C2E()` and serializes `name/screen/type/start/version`

This establishes that `type=6.name` is a local resource filename / pending-download key, not the original business meaning of `0x1B/12.name`.

Supporting static evidence:

- resource open helper: `sub_100D2BE()` at `0x0100D2BE`
- local resource stream wrapper: `load_resource_stream_by_name()` at `0x0100D498`
- resource pending-table writer: `sub_1044DA0()` at `0x01044DA0`
- pending callback marker: `sub_1044EF6()` at `0x01044EF6`
- request builder: `send_update_chunk_request()` at `0x01036C66`
- pending-entry copy helper: `sub_1036C2E()` at `0x01036C2E`

Additional confirmed callsite narrowing:

- `sub_100D2BE()` currently has only four code xrefs in `江湖OL.CBE`:
  - `sub_100D338()` at `0x0100D348`
  - `load_resource_stream_by_name()` at `0x0100D498`
  - `sub_1043206()` at `0x01043216`
  - `sub_1044DA0()` at `0x01044DB4`
- the two post-login/resource-update candidates in scope are `sub_1043206()` and `sub_1044DA0()`
- inside `send_update_chunk_request()` case `n2==4`, the serialized download key comes from the current pending entry at `*(R9+38284) + 2`, while `type` comes from the first `u16` of that same entry, `start` comes from offset `+52`, and `version` comes from `R9+38328`
- `sub_1036C2E()` flips the pending-entry state byte from `1` to `2` when it selects the matching entry for serialization

Latest confirmed runtime narrowing:

- the first observed reuse of the `0x1B/12`-derived UI name `MMORPGTempcbm` is not through `sub_1044DA0()/sub_1044EF6()` and not through `type=6`
- instead, after `kind=27 subtype=12` is consumed, the live path is:
  - `parse_actor_motion_descriptor()` callsite `0x0100D6FC`
  - `sub_100D534()`
  - `load_resource_stream_by_name()` at `0x0100D48A`
  - `sub_100D2BE()`
- in the newest trace, those four steps all carry the same name as the active UI cached name:
  - `name=MMORPGTempcbm`
  - `uiName=MMORPGTempcbm`
- other currently traced callers into `sub_100D534()` (`sub_100D564()` at `0x0100D570` and `sub_100DB82()` at `0x0100DB9C`) do not carry that same `MMORPGTempcbm` string on the observed path; they continue to load normal actor/UI resource names such as `h_warrior.actor`, `ui_h_war.actor`, and map/decoration actor resources

Latest confirmed upstream caller:

- the first confirmed upstream caller that feeds the misrouted name into `sub_10352AE()` is `scene_rebuild_runtime_nodes()` at `0x0100F8F6`
- runtime traces show this exact chain after `kind=27 subtype=12`:
  - `trace_sub_10352ae_call_from_scene_rebuild ... stack0Name=MMORPGTempcbm uiName=MMORPGTempcbm`
  - `trace_parse_actor_motion_entry ... arg4Name=MMORPGTempcbm uiName=MMORPGTempcbm`
  - `trace_resource_stream_call_from_actor_motion ... name=MMORPGTempcbm uiName=MMORPGTempcbm`
- no competing `trace_sub_10352ae_call_from_1036768` line carries that same string on the observed path

This establishes the currently active misroute as:

- `0x1B/12.name`
- copied into active UI name cache (`+1141`)
- reused by `scene_rebuild_runtime_nodes()` as the stacked extra argument for `sub_10352AE()`
- forwarded into `parse_actor_motion_descriptor()`
- treated as a motion-resource name by `load_resource_stream_by_name()`

Additional confirmed asset-format constraint:

- falling back to a normal on-disk actor resource such as `h_warrior.actor` is not a correct long-term substitute for the missing motion descriptor
- runtime evidence:
  - `scene_rebuild_runtime_nodes()` now forwards `stack0Name=h_warrior.actor`
  - `parse_actor_motion_descriptor()` consumes `arg4Name=h_warrior.actor`
  - `load_resource_stream_by_name()` opens the real file successfully
  - but the next resource-open attempts become malformed non-field strings such as `iorwalk1.gif...h_warriorwalk2.gif...jian.gif`, which shows the parser is interpreting the file with the wrong schema
- local asset evidence:
  - scanned `bin/JHOnlineData/*.actor` files in the repo all currently carry type byte `0x02` at offset `+4`
  - none of the tracked local `.actor` files carry the mock motion-wrapper marker `0xF1`

This means the repository currently has many ordinary actor/image-sequence `.actor` files, but no confirmed real motion-descriptor sample matching what `parse_actor_motion_descriptor()` expects.

### First-Scene Local Resource-Miss Request After HUD Seed Fallback

Status: request timing/shape `confirmed`; exact semantic family `hypothesis`

Newest confirmed runtime sequence on the current branch:

- `trace_status_meter_seed_fallback` fires once on the first-scene empty-head HUD path, after which `scene_draw_status_panels()` reaches divide sites with `displayMax=120/100`
- the same session then reaches `scene_runtime_tick label=actor_pass`
- immediately afterwards, local resource open fails through `sub_100D338() -> sub_100D2BE()` for GBK name `侠剑江湖`
- the client then sends a new unhandled `WT len=49` request whose decoded object sequence is:
  - `1/12/1`
  - `1/7/42`
  - `1/6/1`
  - `1/6/13`
  - `1/6/14`
  - `1/2/10`
  - `1/25/5`
- the same payload also carries field `Type=101`

Why this matters:

- the missing local key is the selected role name, not a known asset filename
- this makes the next blocker look less like a generic scene-channel stall and more like a resource-key misuse on the first in-scene actor/bootstrap path

Current best inference from static + runtime evidence:

- the request likely belongs to the net-manager missing-resource/update family rather than to the earlier scene-business `0x1E` / `0x1B` protocol chain
- evidence for that inference:
  - the packet is emitted immediately after a direct local-open fail
  - the net manager already has a confirmed resource-miss queue path (`get_net_manager_object()+56`, later `send_update_chunk_request()` for `type=6`)
- this is still only `hypothesis` until the new queue-entry traces confirm which enqueue method and request-type produced the `len=49` packet

Current narrowed response-side static facts:

- `1/2/10` lands in `net_handle_actor_move_info()` case `10`, which calls `sub_1012958()` and only iterates `otherinfo` when field `othernum > 0`
- `1/6/1` lands in `net_handle_task_response_dispatch()` case `1`, which treats field `taskinfo` as a counted blob and tolerates a zero-length / zero-count path
- corrected 2026-06-08: `1/6/13` is now confirmed safe for the all-empty tasktype shape. The task dispatcher case `13` walks six `tasktypes` entries; the confirmed record grammar is raw field payload containing six `tagged i8 + len16 C string` records. Older same-window crashes came from an extra blob-wrapper length and then from using tagged i16 instead of tagged i8.
- `1/6/14` has a confirmed safe zero-item branch: case `14` first reads field `action`; when `action==0`, it reads `tasknum` plus `taskinfo` and tolerates `tasknum=0` with an empty blob
- `1/25/5` still has no confirmed direct local consumer, but returning the same subtype with a minimal numeric `result` shell is structurally harmless; the older `1/25/12 {result=4}` clear path remains separately confirmed in `net_handle_info_banner_state()`

Current emulator-side compatibility response:

- `src/main.c` now contains a very narrow built-in `builtin-scene-resource-followup` reply for this exact `len=49` / `Type=101` packet shape
- current synthetic reply objects are:
  - `1/12/1 {learnednum=0, learnedskill=\"\"}`
  - `1/7/42 {booknum=0, booksinfo=\"\"}`
  - `1/6/1 {taskinfo=<empty blob>}`
  - `1/6/13 {tasktypes=<6 x tagged-i8 + empty C-string record>}`
  - `1/6/14 {action=0, tasknum=0, taskinfo=<empty blob>}`
  - `1/2/10 {othernum=0}`
  - `1/25/5 {result=4}`
- status: `confirmed`
- the next rerun proves the fuller 7-object reply is accepted on the live scene path:
  - `bin/logs/net_packets.log` shows `source=builtin-scene-resource-followup responseLen=246` for the exact former `WT len=49` request
  - `bin/logs/net_trace.log` logs `mock_scene_resource_followup_response objects=7 ... len=246` followed by `handled_packet ... count=7`
  - immediately after the queued `event=7` is consumed, `loadFlags` drop from `1,0,0` to `0,0,0` and the same session continues through repeated `scene_runtime_tick label=draw_pass/status_panels/actor_pass`
  - the same latest tail contains no new `send_payload`, no new unhandled-packet marker, and no assert/abort in `stdout_trace.log`
- the next manual in-scene interaction confirms the same conclusion from the other side too: after the accepted `len=49` follow-up, later touch events generate only local `touch_dispatch` trace lines and still do **not** emit a newer outbound WT packet
- implication: once this exact `1/12/1 + 1/7/42 + 1/6/1 + 1/6/13 + 1/6/14 + 1/2/10 + 1/25/5` family is answered, the visible center/bottom progress strip is no longer evidence of a missing server response by itself; the next blocker has moved into local scene/UI state
- the newest rerun keeps that protocol conclusion intact: even after `trace_loading_overlay_suppressed` begins firing for scene-bootstrap `sub_1003568()` calls, the remaining strip is still drawn locally in the settled scene and no newer outbound WT request appears. The current blocker remains inside scene/UI draw state, not packet sequencing.
- the next rerun narrows that local state further: the persistent strip is the scene-runtime `loading.gif` widget created by `scene_get_loading_gif_widget()`, not any additional wire-level wait. No newer WT request accompanies its steady-scene draw path.
- the latest rerun keeps the protocol conclusion unchanged even after partial UI suppression: once the scene `loading.gif` widget is disabled, no new outbound WT appears and the remaining strip fragments still come from local picture-library draw helpers. The current blocker is still purely local scene/UI behavior.
- a later manual in-scene click exposed one more genuine wire-level gap unrelated to the strip itself: the client can also send a standalone one-object `WT len=19 hdr=2/10 objs=1/2/10` after scene entry. Before this round, only the bundled `len=49` scene-followup path answered `1/2/10`, so the standalone request fell through to `assert(0)`.
- status: `partial -> now implemented`
- current mock behavior now also accepts the standalone `1/2/10` shape and returns the same minimal object as the bundled follow-up:
  - `1/2/10 {othernum=0}`

## Login Protocol

### Overview

The runtime login UI lives in `mmTitleMstarWqvga.cbm`.

Status: `confirmed`

Login-form flow:

- render: `0x2C96 -> login_form_render`
- touch dispatch: `0x2BE0 -> login_form_handle_touch`
- button/selection action: `0x2B1E -> login_form_handle_action`
- submit: `0x1E40 -> login_form_submit`
- request builder: `0x1B9C -> net_build_login_request`
- primary response handler: `0x16DC -> net_handle_login_response`

### Login Request

Status: `confirmed`

Account/password submit path:

`login_form_submit -> net_build_login_request(1, 1, 6)`

Request fields emitted by `net_build_login_request`:

- `coreVer`
- `appVer`
- `userName`
- `password`
- `imsi`

Notes:

- `userName` is used on the direct account/password submit path.
- A nearby alternate path uses `username` instead of `userName`. Status: `confirmed` for the existence of both spellings in client code, `hypothesis` for requiring server compatibility with both until a real server trace or deeper handler analysis confirms it.

### Request / Owner Split

Status: `confirmed`

The title module has two distinct login request families, not one request with interchangeable field spellings:

- `title_four_choice_screen_handle_action()` (`0x2724`)
  - when local `choiceSel == 0`, confirm/center input sends `net_build_login_request(1, 12, 5)`
  - this is the four-choice screen's direct-enter / alternate login path
- `login_form_handle_action()` (`0x2B1E`)
  - on action `4096`, after non-empty username/password checks, it calls `login_form_submit()`
  - `login_form_submit()` (`0x1E40`) sends `net_build_login_request(1, 1, 6)`
  - this is the explicit account/password submit path

`net_build_login_request()` (`0x1B9C`) also uses different credential sources on those two branches:

- request subtype `1`
  - writes `userName`
  - pulls username/password from the live edit buffers at `r9+10816` / `r9+10812`
- request subtype `12`
  - writes `username`
  - pulls username/password from the saved `mmorpg_LoginRecord` block at `r9+10772 + 16/+48`

Current high-confidence comparison:

| Local trigger | Request on wire | Request-side fields | Response owner | Success routing | Likely local destination |
| --- | --- | --- | --- | --- | --- |
| Four-choice screen, `choiceSel == 0`, confirm/center | `1/1/12` | `coreVer`, `appVer`, `username`, `password`, `imsi` | `0x2A50 -> login_alt_response_dispatch_wrapper` | `login_alt_result_dispatch() -> login_stage_success_dispatch()` | stage-dependent title target; `stageFlag==1` returns to the first local title screen, `stageFlag==4` enters the role-list family |
| Login form confirm (`4096`) | `1/1/1` | `coreVer`, `appVer`, `userName`, `password`, `imsi` | `0x2D80 -> login_primary_response_dispatch` | `net_handle_login_response(packet, 1) -> login_response_result_dispatch()` | primary success target at `+0x4C`, i.e. the server-list / role-list composite family before later role selection |

Implication:

- `1/1/12` should not be treated as a harmless spelling variant of `1/1/1`
- if the emulator answers a `1/1/12` request with the `1/1/1` primary-success contract, or vice versa, the packet may still look "login-shaped" but will route through the wrong owner and wrong local target family
- for the expected server-list selection flow, the highest-confidence path remains the explicit login-form `1/1/1` request followed by a primary `1/1/1` response carrying `result=1`, `serverinfo`, `servernum`, and `newVer`

### Login Response Dispatch

Status: `confirmed`

Two wrappers feed login responses into `net_handle_login_response`:

- `0x2D80`
  - matches `packet[4] == 1 && packet[8] == 1`
  - calls `net_handle_login_response(packet, 1)`
  - this is the primary account/password login response path
- `0x2A50`
  - matches `packet[4] == 1 && packet[8] == 12`
  - calls `net_handle_login_response(packet, 0)`
  - this appears to be a related alternate login/enter flow

### Response Fields

Status: `confirmed`

Fields consumed by `net_handle_login_response`:

- `result`
- `serverinfo`
- `servernum`
- `newVer`
- `information`
- `username`
- `password`

Behavior:

- `result` is treated as an ASCII digit, not a numeric integer.
- current emulator note: title-login mock packets that route through `login_response_result_dispatch()` must therefore encode `result` as raw byte `'1'`, `'2'`, etc., not numeric `0x01/0x02`. A live rerun confirmed that numeric `0x01` reaches `login_result_dispatch_entry` as `resultByte=1` but does not hit the success branch, while the same path statically switches on `'1'`.
- `serverinfo/servernum/newVer` are parsed into the in-memory server list/state when present.
- for `result == '3'` or `result == '4'`, the handler also copies returned `information`, `username`, and `password` into login-related buffers.
- for `result == '2'`, the handler copies `information` into a message buffer and marks a local error state.

### Success Path

Status: `confirmed`

Primary success path after account/password submit:

1. `login_form_submit` copies current edit buffers into `mmorpg_LoginRecord`.
2. `net_build_login_request(1, 1, 6)` sends the login request.
3. response wrapper `0x2D80` validates `type=1, subcmd=1`.
4. `0x2D80` calls `net_handle_login_response(packet, 1)`.
5. if `result == '1'` and the "save default account" flag is set, `0x2D80` calls `0x1430` to persist the active login record.
6. `0x2D80` then calls `0x23C0(resultChar)`.
7. `0x23C0('1')` calls the generic next-step callback object at `R9+0x28CC`, method slot `+0x14`, with target `*(R9+0x29E8 + 0x4C)`.

Newest runtime confirmation:

- once the mock `result` field is encoded as ASCII `'1'`, the live path reaches `login_result_dispatch_success`, swaps the active local object from `login_form` (`05010db0`) to the `+0x4C` family (`0501f838`), seeds `listPtr`, and immediately emits a live `1/1/16` request
- this makes `1/1/16` the next required server/mock contract after clean primary success, not an optional side experiment
- the current minimal follow-up `1/1/16 {result=1} + 1/1/4 {actorinfo}` is also accepted on the same live path: both subtype `16` and subtype `4` are forwarded into the active `target4c` family object, after which the local object advances into the sibling `target50` family

Additional confirmed local-state details:

- `login_form_init()` (`0x2AA8`) explicitly sets `*(r9+10735) = 1`, so the account/password login form's "保存为默认帐号" checkbox is on by default on that path.
- `0x23C0('1')` is a local UI/screen transition, not a direct packet sender.
- the same callback object/method is reused by the role-list parser:
  - `sub_247A()` calls `R9+0x28CC -> +0x14` with target `*(R9+0x29E8 + 0x50)`
  - this means `+0x4C` and `+0x50` are sibling screen/state targets, not ad hoc function callbacks
- the target family behind the later `+0x50` role-list path is now clearer:
  - `sub_4290()` dispatches by `*(r9+10748)`: `0 -> sub_3ECE()`, `1 -> sub_4016()`, `2 -> sub_3FD6()`
  - `sub_3AD2()` resets that family and explicitly sets `*(r9+10748) = 0`
  - `sub_39A4()` explicitly sets `*(r9+10748) = 1` and prepares the local role-entry buffers
  - `sub_3544()` (the confirmed `type=1, subcmd=4` role-list parser) finishes by calling `sub_247A()`

Current highest-confidence interpretation:

- primary `1/1/1` success with `serverinfo/servernum/newVer` transitions into a local server-selection / next-screen target at `+0x4C`
- later `1/1/4` role-list success transitions into the sibling role-selection target at `+0x50`
- this is why a stripped validation reply can consume fields correctly yet emit no immediate follow-up packet: the next step is first a local screen/state switch, not a mandatory network send

Important current mock caveat:

- on the account-login branch, the current built-in mock can bypass this wrapper entirely if it answers a `1/1/1` request with an object whose response subtype is not `1`
- existing runtime evidence shows exactly that mismatch on one live branch:
  - request: `1/1/1`
  - built-in response label: `source=builtin-login`
  - actual response object header: `1/1/6`
- when that happens, title wrapper `0x2D80` fails its `subcmd == 1` check, so `net_handle_login_response()` is not called at all on that path
- this is why fields such as `serverinfo/servernum/newVer` and the later default-account save path can appear "missing" even though the request was recognized as `builtin-login` by the host

### Login Success Branch Split

Status: `mixed`

Confirmed title-module behavior:

- the role-list UI has its own later actor-confirm submit path in `mmTitleMstarWqvga.cbm`
- `sub_4016()` handles confirm/select actions on the role-list screen
- when the user confirms a role, `sub_4016()` calls `sub_3E66(...)`
- `sub_3E66()` builds a network request containing the literal field name `actorinfo` plus three selection-derived values
- there is also a separate title-module response handler `sub_3544()` for `type=1, subcmd=4`
- `sub_3544()` reads fields `result`, optional `servconf`, and then a field also named `actorinfo`
- in that `subcmd=4` path, `actorinfo` is parsed as a counted role-entry list (capped to 5 entries locally), not as the later in-scene actor-status blob
- after parsing that list, `sub_3544()` calls `sub_247A()`, which invokes the same generic next-step callback slot `*(r9 + 10464)()`
- the decompiler shorthand `*(r9 + 10464)()` is now known to be the same router method used by `0x23C0`: `R9+0x28CC -> +0x14`
- that role-list success callback uses target `*(R9+0x29E8 + 0x50)`, whereas the primary `1/1/1` success path uses sibling target `*(R9+0x29E8 + 0x4C)`
- the local composite server/role screen behind that path is stateful:
  - `*(r9+10748) == 0` routes input into `sub_3ECE()` (server-selection navigation)
  - `*(r9+10748) == 1` routes input into `sub_4016()` (role-selection / confirm)
  - `*(r9+10748) == 2` routes input into `sub_3FD6()` (follow-on editor/create-name path)
- `sub_3ECE()` can auto-promote from server mode into role mode by calling `sub_39A4()` when the current server list count is `1`
- `sub_3544()` is also stateful across two subtype values:
  - when `subtype == 16`, it reads and caches `result`
  - only when that cached result is `1` does a later `subtype == 4` packet actually parse `actorinfo`
- current runtime request evidence now adds one more confirmed split point: the live account-confirm tap is sending a top-level `1/1/12` login request (`send_payload ... 01010c00 ... coreVer/appVer/username/password/imsi`), not the earlier assumed `1/1/6`

Current higher-confidence interpretation:

- the branch between "login succeeded" and "show role list" is more likely controlled by outer response stage/wrapper (`subcmd=1` versus `subcmd=4`) than by a magic inner field inside the `subcmd=1` login-success object's `actorinfo`
- the active post-login title screen may also be using the alternate `subcmd=12` success chain (`sub_2A50 -> login_alt_result_dispatch -> login_stage_success_dispatch`) before any role-list-specific stage is even eligible
- reusing the same field name `actorinfo` across these title/game stages is now a confirmed source of confusion: title `subcmd=4.actorinfo` behaves like a role-list payload, while main-CBE login `actorinfo` is the later actor/player blob parsed by `parse_actorinfo_response()`

Confirmed runtime evidence from the current mock branch:

- when the login-success mock returns `actorinfo` immediately, the client does not stop on a role-list request
- the next observed traffic jumps straight into the later post-login game chain (`type=1/2/3`), eventually reaching fresh-enter scene loading
- no separate `0x0A/0x20` role-list request appears on that actorinfo-first path
- the first post-login game packet is not just `1/5/10 id=...`; it is a bundled WT request containing both `1/5/10 id=10001` and `1/7/7 type=1`
- previous built-in replies only answered that bundle with group info plus `0x0A/0x1A` and scene `0x1E/1/3/7`, which left no top-level `7` family response before the first HUD draw
- a focused `1/7/8` experiment was tried next: append one top-level `1/7/8` object carrying `type=4, result=1, seq=1`, because static `sub_1033544()` analysis shows that exact branch can produce the missing `type=15` HUD source head and trigger `scene_rebuild_status_meter_node(2) -> scene_rebuild_status_meter_node(1)` when the source head is still empty
- that experiment is now confirmed unsafe on the default path. Runtime does reach `trace_business_handler ... kind=7 subtype=8`, but then crashes earlier inside `sub_1033544()` because the branch first clones local pointer `*(R9+38020)`, which is still null on the current post-login path. The experiment therefore remains useful as evidence, but is now gated behind `CBE_GROUP_TYPE1_MISC_SYNC8=1` instead of staying enabled by default

More precise current reading of that bundled post-login request:

- `source=builtin-group-type1` is not one clean real server answer yet; it is a mixed convenience bundle the emulator currently uses to push the client forward
- the bundled request is not emitted by the title/login screen. Runtime and static evidence now both place it in `scene_runtime_init_and_sync()` on the first scene screen (`activeScreen=01053450`):
  - `bin/logs/net_trace.log` shows the `len=35` send after `trace_sub_1010228_callsite label=scene_runtime_init_and_sync ... activeScreen=01053450`
  - static `scene_runtime_init_and_sync()` at `0x01013594..0x01013636` first calls `sub_1010228()`, then queues three outgoing `major=1, kind=7, subtype=7` requests with field `type = 1..3`
  - the same block uses the scene-local queue/dispatcher objects at `R9+0x5554` and `R9+0x5520`, so this is a scene-bootstrap follow-up sequence, not a login-form request
- `sub_1010228()` is the pre-send helper that explains why the first `type=1` request is bundled with `1/5/10`:
  - it rebuilds the 0x130-byte local group/party snapshot at `*(R9+0x5CE4+0x10)`
  - it copies current node fields such as id/name/status bytes from `*(R9+0x5C64+0x40)`
  - it sets local send/state flags at `R9+0x5C74` and `R9+0x5C79` to `1`
  - runtime evidence already shows the first queued `type=1` leaves as the combined `1/5/10 id=10001 + 1/7/7 type=1` packet, while later `type=2` and `type=3` are sent alone
- the `1/5/10` half does correspond to a real client request and a real handler family:
  - response object `1/5/10` lands in `net_handle_group_info()` subtype `10`
  - that subtype reads `num`, `groupinfo`, and `leadid`
  - `groupinfo` is parsed as a counted blob of group-member records, looped `num` times, and each decoded entry is forwarded into `sub_1011E1E(...)`
  - `leadid` is used as the active/leader id for subtype `10`
  - unlike subtype `3`, subtype `10` enters the parse path unconditionally, so `result` is not the decisive gate there
- the current `0x0A/0x1A` object is also backed by a real handler, but it belongs to a different family:
  - it lands in `net_handle_role_login_gift_glamour()` subtype `26`
  - that branch reads only `name` and `money`
  - it copies `name` into the three local role/status objects and mirrors `money` into the same trio
  - extra fields the emulator currently sends there such as `result`, `type`, and `npcnum` are not used by subtype `26`
- the appended `0x1E/1`, `0x1E/3`, and `0x1E/7` objects are scene-channel helpers:
  - `0x1E/1` consumes `scene + posinfo`
  - `0x1E/3` expects `curpage/pagenum/colnum/colnames/roomnum/roomlist/npcnum/npcinfo`
  - `0x1E/7` expects `roomid/colnum/colnames/rolenum/rolesinfo`

Current implication:

- the bundled request is best understood as "group info request + another top-level `7` family sync request", not as a single monolithic semantic
- more specifically, `1/7/7` now looks like a scene-side "misc player fields" bootstrap request family:
  - the sender is in scene init, not title/login
  - the live sequence always emits `type=1`, `type=2`, and `type=3` together during the same first-scene bootstrap window
  - current confirmed response consumers fit that reading: `type=2` expects a later top-level `7/20` carrying `pcimg`, and `type=3` expects top-level `7/32` carrying `expcard`
  - `type=1` is the broadest member of the trio and is the one currently still missing its true top-level `7` reply; it is also the only one that leaves bundled with group info
- the emulator's current `builtin-group-type1` response only answers the group half faithfully
- the other half, `1/7/7 type=1`, still lacks its true top-level `7` reply; the current `0x0A/0x1A + 0x1E/1/3/7` additions are useful scene-bootstrap shims, but they are not yet proven to be the real server contract for that request

Hypothesis / current server-mock reading:

- returning `actorinfo` in the initial login-success payload is effectively equivalent to pre-selecting a role
- the safer role-list-first server behavior is therefore:
  - login success still returns a normal success object so the title module can leave the login form
  - the actual role-list content may need to arrive as its own `type=1, subcmd=4` stage rather than as extra objects stuffed into the initial `subcmd=1` success packet
  - `actorinfo` is withheld until the client later submits the explicit role-selection request

Current built-in mock experiment:

- `src/main.c` now defaults to a staged retry instead of the previous same-packet mixed-object retry
- the primary login reply still uses the normal actor-success object so the current title/game parser contract remains satisfied
- immediately after that login reply is queued, the mock now queues a second `event=7` packet containing a dedicated title role-list object
- that follow-up packet is currently built as two title-side objects:
  - `major=1, kind=1, subtype=16` with `result = 1`
  - `major=1, kind=1, subtype=4` with `actorinfo = counted role-entry table`
- the counted role-entry table is currently modeled from `sub_3544()` as:
  - count
  - repeated `u32 roleId, u8 job, u8 sex, string roleName, u32 level`

Implementation note for the current mock branch:

- the live `1/1/12` path must be keyed from the WT header `kind/subtype` bytes `request[5]/request[6]`
- reading `request[6]/request[7]` on the observed `01010c00` login request misclassifies the stage as `12/0`, which prevents the alternate-success shell and the intended `after-main` staged role-list follow-up from firing

Newest order-specific runtime evidence:

- the mixed same-packet experiment is now confirmed sensitive to object ordering as an open question
- `rolelist + actorinfo` in one login-success packet still auto-enters scene on the current branch
- the next active experiment is `actorinfo + rolelist`, to test whether a later role-list object can override the earlier actor-success side effects inside the same event

Supporting evidence:

- title-module role confirm path: `sub_4016()` (`0x4016`) and `sub_3E66()` (`0x3E66`) in `mmTitleMstarWqvga.cbm`
- title-module role-list response path: `sub_3544()` (`0x3544`) in `mmTitleMstarWqvga.cbm`, which validates `type=1, subcmd=4` and parses counted `actorinfo` entries
- current runtime log: `bin/logs/net_trace.log` shows `mock_login_actor_response ...` immediately followed by outgoing `type=1`, `type=2`, and `type=3` packets, with no intervening built-in role-list request

### Non-Success Result Routing

Status: `confirmed`

Post-handler routing from `0x23C0`:

- `result == '1'`
  - invoke generic success callback `*(v1 + 10464)()`
- `result == '2'`
  - show a client message via `sub_10C6(&unk_258C, 0)`
- `result == '5'`
  - show a client message via `sub_10C6(&unk_2580, 0)`
- `result == '6'`
  - show a client message via `sub_10C6(&unk_25A0, 0)`

### Alternate Success Path

Status: mixed

Confirmed alternate login-related response path:

1. wrapper `0x2A50` validates `type=1, subcmd=12`
2. it calls `net_handle_login_response(packet, 0)`
3. it then calls `0x19C2(resultChar)`
4. `0x19C2('1')` falls through to `0x1956('1')`
5. `0x1956('1')` dispatches to a stage-dependent callback:
   - if local stage flag `*(r9+8276)+357 == 1`, call callback stored at `*(v1 + 10800)`
   - if local stage flag `*(r9+8276)+357 == 4`, call callback stored at `*(v1 + 10804)`

Status notes:

- `confirmed`: wrapper checks, call chain, and callback dispatch exist as described
- `hypothesis`: the gameplay meaning of subcmd `12`, stage flag values, and the two callbacks still needs runtime confirmation

Additional current-mock implication:

- the alternate wrapper still does call `net_handle_login_response()`, but it does **not** contain the later default-account persistence logic from `0x2D80`
- current built-in `1/1/12` success shells are also minimal (`result=1` only), so even when this path is hit there is no `serverinfo/servernum/newVer` payload to populate the local server-selection tables
- current `src/main.c` default mock split now mirrors the request family directly:
  - `requestSubtype == 1` returns `vm_net_mock_build_login_primary_validation_response()`
  - `requestSubtype == 12` returns `vm_net_mock_build_login_alt12_success_response()`
- current validation build now narrows the queued `1/1/15 {result=1}` follow-up to the alternate `1/1/12` request family only
- implication: primary `1/1/1` success can now be tested as a cleaner control path, while the older alternate `1/1/12` branch still keeps its previous staged-mode follow-up behavior
- latest clean-control runtime result is negative but useful:
  - the rerun shows only the expected primary request/response pair (`send_payload ... hdr=1/1`, `mock_login_primary_validation_response top=1,1,1 ...`, `handled_packet ... hdr=1/1 objs=1/1/1`)
  - there is no queued `1/1/15` follow-up in the same window
  - despite that, the live callback owner at delivery time is still `dispatch=05017509`, and `trace_title_login_state` is unchanged across `pre/post_data_event_05017509`
  - sampled fields remain `mode=0`, `listPtr=0`, `roleFamily=0`, `activeScreen=010534b4`
- updated implication: removing `1/1/15` contamination is not enough to restore the expected server-list flow. On the current path, a clean primary `1/1/1` success is still being delivered while `sub_938` owns the callback window, so the remaining blocker is more likely "who should switch the active response owner/target before primary success lands" than "which extra follow-up packet should repair things afterward"

## Business Scene-Entry Notes

Business-network entry in the main CBE is `0x01012E4C`.

Status: mixed

Confirmed fields seen across mocked game responses:

- login/role:
  - `actorinfo`
  - `playerinfo`
- scene/map:
  - `scene`
  - `posinfo`
  - `npcnum`
  - `npcinfo`
- resource/update:
  - `version`
  - `start`
  - `totalsize`
  - `crc`
  - `data`
  - `result`
- other observed payloads:
  - `rolesinfo`
  - `roleinfo`
  - `iteminfo`
  - `giftinfo`
  - `battleinfo`

For current `type=2/3` game-entry responses, the mock needs:

- default response subtype `0x1A`
- `scene = "00蓬莱仙岛_01"` as GBK bytes
- `posinfo` carrying two tagged `i16` coordinates. The current mock default is `223,382`, matching the first entry spawn point parsed from `bin/JHOnlineData/c00蓬莱仙岛_01.sce`.

If actor resource offsets need deeper decoding later, prefer reverse-engineering the `actorinfo` layout from CBE `0x0100FA88` rather than forcing actor state by direct global writes.

Status notes:

- `confirmed`: the listed field names are observed in the client and current mock flow
- `confirmed`: current mock-driven entry requires a scene-channel object carrying GBK `scene` and `posinfo` to progress
- `hypothesis`: exact semantics and full binary layout for `actorinfo`, `playerinfo`, `npcinfo`, `rolesinfo`, `roleinfo`, `iteminfo`, `giftinfo`, and `battleinfo` are not yet fully decoded

### `actorinfo` trailing string and `R9+0x5E46`

Status: `mixed`

Confirmed parser behavior:

- `parse_actorinfo_response()` / `parse_actorinfo_playerinfo_blob()` at `0x0100FA88` writes directly to the fresh-enter text buffer `R9+0x5E46`
- the write happens on the `a2 == 0` `actorinfo` path at `0x0100FD00 .. 0x0100FD08`
- the source is a late string field from the `actorinfo` blob, copied with `sub_100FA56(srcReader, R9+0x5E46, 0x1E)`
- the same function returns `R9+0x5E46` at `0x0100FD24 .. 0x0100FD2E`

Confirmed neighboring field shape:

- immediately before that string copy, the parser reads another scalar field and stores it both into the actor structure and, on the `actorinfo` path, into scene/global state
- immediately after the `R9+0x5E46` copy, the parser reads two more trailing scalar fields into actor structure slots
- this matches the current mock builder pattern in `src/main.c:vm_net_mock_build_actor_info()`, where the final sequence is:
  - string
  - scalar
  - string
  - scalar
  - scalar

Hypothesis:

- the second trailing string in `actorinfo` is much more likely to be a scene/map key or scene resource identifier than a motion-resource filename
- evidence supporting that interpretation:
  - fresh-enter later reuses `R9+0x5E46` as `currentMapIdText`
  - the same value is validated by `sub_100EEBC()` / `sub_100FA40()`
  - restore/fresh-enter logic later reuses the derived persistent slot as outgoing `mapID`
  - local assets already include scene resources named like `c00蓬莱仙岛_01.sce` and `c00蓬莱仙岛_03.sce`

Open mismatch to keep in mind:

- the current mock builder still fills that second trailing `actorinfo` string from `motionResource`
- runtime/static evidence now strongly suggests that semantic is wrong or at least incomplete
- this does **not** yet prove the exact canonical wire value:
  - it may be a full scene filename such as `c00蓬莱仙岛_01.sce`
  - or an extensionless/internal scene key later mapped to the `.sce` asset

Current mock experiment:

- `src/main.c` now routes that second trailing `actorinfo` string, and the synthetic `0x1B/12.name`, through a dedicated `vm_net_mock_scene_key_name()` helper instead of `motionResource`
- default mock value remains the existing GBK scene key `c00蓬莱仙岛_01`
- `CBE_SCENE_KEY` can now override this field for targeted reruns

Latest confirmed local-load behavior after that change:

- the extensionless scene key is now consumed as a real local scene resource, not as an actor/motion resource
- storage trace shows:
  - `file_open_resolve_map_extension from=JHOnlineData/c00蓬莱仙岛_01 to=JHOnlineData/c00蓬莱仙岛_01.sce`
  - the `.sce` payload contains `SCE2` plus embedded reference `00蓬莱仙岛_01.map`
  - the client then opens `JHOnlineData/00蓬莱仙岛_01.map`
- the `.map` payload expands into scene art such as `m_peak1.gif`, `m_village05.gif`, and `m_apotheosis.gif`
- this strongly confirms that the corrected field belongs to the local scene/map resource family

### `actorinfo` mid-body 20-byte string and `parse_actor_motion_descriptor()`

Status: `confirmed`

Confirmed parser/runtime behavior:

- `parse_actorinfo_response()` at `0x0100FA88` contains an earlier two-string block before the final `actorResource + i16 + sceneKey + i16 + i16` tail
- on the `a2 == 0` actor path, those two strings are copied with `sub_100FA56()` into actor-structure slots `actor+0x100` (size `10`) and `actor+0x10A` (size `20`)
- a later separate string field is copied into `actor+0xD8` before the final scene-key copy into `R9+0x5E46`
- newest `scene_draw_actor_pass()` trace now pins `actor+0x10A` directly: the late failing open is `trace_resource_open_helper ... namePtr=0540010A name=h_warrior.actor`, i.e. the direct image path is consuming the 20-byte mid-body slot itself

Latest runtime evidence:

- the same clean rerun still shows a correct `.actor -> inner gif` chain during scene bootstrap:
  - `trace_resource_stream_call_from_db82 ... name=h_warrior.actor`
  - then repeated `trace_resource_open_helper ... lr=0100d34d ... name=h_warriorwalk1.gif`, `h_warriorwalk2.gif`, `jian.gif`, `guang1.gif`, ...
- but the later actor-pass failure is different:
  - `trace_scene_runtime_tick label=actor_pass ... tick=197/291`
  - immediately followed by `trace_resource_open_helper ... lr=0100d34d ... namePtr=0540010A name=h_warrior.actor`
- static IDA for `parse_actorinfo_response()` plus that `0540010A` pointer makes the active mismatch concrete: the mid-body 20-byte slot is a direct image/decode input, while the later `actor+0xD8` string remains the real actor-container field

Implication:

- this mid-body 20-byte `actorinfo` string is not safe to treat as a role display name
- it is also not safe to treat as the later `.actor` container field
- current strongest fit is “direct preview/frame image resource name”, while the later `actor+0xD8` field remains the `.actor` resource used by the normal descriptor parser path

Current mock change:

- `src/main.c:vm_net_mock_build_actor_info()` now fills that specific 20-byte mid-body string from a new direct-image helper (`h_warriorwalk1.gif`, `hW_warriorwalk1.gif`, `h_assassinwalk1.gif`, `hW_assassinwalk1.gif`, `h_magewalk1.gif`, `hW_mage1.gif`) instead of `roleName` or `.actor`
- the later trailing actor-resource field still uses `vm_net_mock_actor_resource_name()` and remains the source for the correct `.actor -> inner gif` parser path
- the later trailing scene-key field remains separate and still uses `vm_net_mock_scene_key_name()`

### Scene `actorinfo` order correction (`a2==0`)

Status: `confirmed`

The earlier “`actorId, visual bytes, name, playerId/display string, then u32 level/current/base/...`” summary was still too coarse. Full decompile of `parse_actorinfo_response()` at `0x0100FA88` now fixes the scene-side order after the second string more precisely:

- tagged `u32 summaryStatus`, then truncated/stored into `actor+0x138`
- `u32 primaryCurrent`, `u32 primaryBaseMax`, `u32 secondaryCurrent`, `u32 secondaryBaseMax`
- `u32 extra132`
- six tagged `u32` values truncated into word-sized status/stat slots around `actor+0x122`
- `u32 summary176`, `u32 gap09C0`
- two `u8` state bytes
- `u32 primaryDisplayMax`, `u32 secondaryDisplayMax`
- two more tagged `u8` fields, stored into word slots around `actor+0x11E/+0x120`
- bounded strings `shortLabel(+0x100)` and `previewImage(+0x10A)`
- three trailing `u32` fields at `+0xCC/+0xD0/+0xD4`
- bounded `actorResource(+0xD8)`, `i16 actorResourceArg(+0xAC)`, bounded global `sceneKey`, then trailing `i16 +0xF0/+0xF4`

Additional confirmed consumers:

- `scene_copy_status_summary_fields()` copies `actor+0x68[0..]` (`id + short text`) plus `actor+0x138`; this confirms the second bounded string at `actor+0x6C` is part of the compact HUD summary family
- `scene_draw_status_panels()` still uses `actor+0xB4/+0xBC/+0xB8/+0xC0` for current/base values and `sceneStatusMeterNode+0xC4/+0xC8` for derived display maxima
- `parse_actor_motion_descriptor()` still consumes the bounded `previewImage` slot at `actor+0x10A`, while the later `actor+0xD8` bounded string remains the real `.actor` resource field

Current mock behavior now follows this corrected order. The previous single `gap0CC0` placeholder was split into three separate trailing `u32` fields, and the display-name string remains a separate field from the later `shortLabel/previewImage` pair.

### `1/2/10 otherinfo` self-actor seed

Status: `confirmed`

`sub_1012958()` (`net_handle_actor_move_info` case `10`) proves that `1/2/10` is not just an optional “others list count” packet. When `othernum > 0`, the parser walks `otherinfo` and calls `scene_node_find_or_create()` for each entry.

Confirmed per-entry order from `sub_1012958()`:

- `u32 actorId`
- `u8 visualVariant`
- `u8 visualGroup`
- `str labelText`
- `u8 targetPosX`
- `u8 targetPosY`
- `str shortLabel`
- `str longLabel`
- `i16 gridPosX`
- `i16 gridPosY`

Current parser-safe mock behavior now returns an empty list for both:

- the bundled scene follow-up `1/2/10`
- the standalone one-object `WT len=19 hdr=2/10 objs=1/2/10`

The current minimal response is `othernum=0` plus empty `otherinfo`. Evidence: IDA pseudocode for `sub_1012958()` shows it reads `othernum` first and only initializes/walks the `otherinfo` stream when `othernum > 0`; it still returns success and writes `*(R9+23836)=1` after the branch. This makes the empty object a confirmed parser-safe response, while the non-empty per-entry record remains a hypothesis until its field order/timing is reverified.

Latest confirmed limitation:

- after the corrected `actorinfo` tail lands, scene loading can now reach a later local crash from the same broad consumer family: `sub_100F094()` calls selector helper `sub_1004E10()` with a live selector object whose internal table pointer at `selector+0x0C` is still null
- this is currently treated as a local scene-init contract gap, not as proof that the non-empty `1/2/10` record shape above is fully semantically correct
- earlier builds carried a blocker-removal guard at `0x01004E10`, but this class of runtime skip guard has since been removed. The remaining work is to identify which real init/resource path should populate the selector table.
- after that guard, the next live crash is even later in the same scene-local family: `scene_draw_status_panels()` reaches the left portrait block and tries to `BLX` `sceneHudCurrentPortraitWidget->draw(+0x18)` with a null callback
- runtime evidence already shows the six portrait UI actor resources (`ui_h_* / ui_hw_*`) loading successfully before this point, so the remaining issue is currently classified as a local portrait-widget init/callback gap rather than a network/resource-body miss
- earlier builds also carried a `0x010146DA` portrait null-callback guard; that guard is removed under the no-parser/draw-bypass rule.

## Legacy Source

`docs/net_mock_protocol.md` is retained as a historical working note and source document. New durable protocol conclusions should be recorded here first, then the legacy note can be trimmed or cross-referenced as needed.

### `event=7` wrapper vs title/login callbacks

Status: `confirmed`

Current emulator/runtime evidence now separates three layers that had previously been conflated during login-flow tracing:

1. queued task callback
   - current queued `event=7` replies use `0x0103489A -> net_wrapper_event_dispatch`
2. net-manager callback
   - `net_wrapper_event_dispatch` first calls `get_net_manager_object()+0x44`
   - runtime watcher reads the same slot as `R9+0x9588+0x44`
3. business/title callback
   - only after the net-manager leg, and only when the current business object state allows it, the wrapper calls `*(R9+0x94A8)+0x14`

Confirmed current-login observation:

- in the “click confirm but no screen transition” runs, `net_state_observe cb44=01037473`
- static RE confirms `0x01037472 -> handle_version_update_response`
- therefore the queued login reply is still entering the wrapper through the version/update-family callback before any title-local callback can drive a `0x23C0` / `0x247A` style screen-switch path

Implication:

- `runtime_state dispatch=05017509` should not be read as “the queued task directly called the title success callback”
- it is only the current business callback stored on the active object
- lack of `trace_title_screen_callback` at `0x23D4/0x248A/0x1984/0x1994` is consistent with the wrapper never reaching those title-success BLX sites

### Title login local state family at `R9+0x29E8`

Status: `confirmed`

The active title/login state block used by the current screen includes at least:

- `+0x00` / runtime `state0`
- `+0x02` / runtime `focus`
- `+0x07` / runtime `saveDefault`
- `+0x14` / runtime `mode`
- `+0x3C` / runtime `listPtr`

Confirmed local routines around this block:

- `0x2620 -> login_record_init_and_load()`
  - initializes/loads `mmorpg_LoginRecord`
  - writes the local `saveDefault/state0` bytes during button-path setup
- `0x53EC`
  - inbound parser for top-level `type=1` family with subtypes `6/7/8/15`
- `0x5324`
  - subtype `7` handler; appends entries into `*(R9+10788)` and updates list-related local state
- `0x39A4`
  - subtype `15` success path; sets `*(R9+10748)=1`

Current implication:

- the active login/selection state machine behind `010534b4` is better explained by a `type=1, subtype=7/15` follow-up family than by the earlier `1/1/4` role-list hypothesis
- this matches runtime evidence where the current `1/1/1` success reply and staged `1/1/16 -> 1/1/4` follow-up produce no writes at all to `mode/listPtr/roleFamily/target*`
- the current default mock experiment has therefore been reduced to the narrowest confirmed gate first: `1/1/15 {result=1}` without the old staged `1/1/4` role-list payload

### Active `010534B4` title screen and `05017509` owner

Status: `confirmed`

The currently active title screen on the stuck login path is now pinned to a specific `mmTitleMstarWqvga.cbm` transition screen rather than to the earlier `login_primary_response_dispatch()` owner.

- runtime `screen_func_table` for `screen=010534b4` maps cleanly to `sub_C82()` with relocation base `0x05016BD0`:
  - `init=05017621 -> sub_A50 + 1`
  - `destroy=050174b1 -> sub_8E0 + 1`
  - `logic=05017411 -> sub_840 + 1`
  - `render=0501716f -> sub_59E + 1`
  - `pause=05017075 -> sub_4A4 + 1`
  - `resume=05017025 -> sub_454 + 1`
- `sub_A50()` also installs the active business callback by writing `*(R9+10312)+20 = sub_938`
- the runtime `dispatch=05017509` snapshot is exactly that installed `sub_938 + 1`
- `sub_938(event=7)` does not route `type=1, subtype=1` login replies at all; it only iterates packet objects where `kind == 1` and `subtype` is one of `2`, `3`, or `6`
- therefore a lone `1/1/1` reply, including `result=2`, cannot enter `login_primary_response_dispatch() -> login_response_result_dispatch()` while this `010534b4/sub_938` screen owns `R9+21812`

Implications:

- the current failure packet is not merely "missing one more follow-up object" for the `010534b4` owner; the active owner ignores subtype `1` entirely
- the fixed primary failure prompt for `result='2'` does not require any extra stage packet once the correct owner is active
- static `net_handle_login_response(packet, 1)` only needs the `result` byte for the primary fixed-prompt branch; copying `information` is only done on the alternate `a2 == 0` path

Related local confirm path:

- the current confirm button does not locally switch `R9+21812` to `login_primary_response_dispatch()`
- static action flow is:
  - `login_form_handle_action(4096)` validates non-empty username/password and calls `login_form_submit()`
  - `login_form_submit()` copies the current edit buffers into `mmorpg_LoginRecord`, writes stage byte `*(R9+0x2054 + 0x104 + 5) = 5`, and sends `net_build_login_request(1, 1, 6)` which appears on the wire as top-level `1/1/1`
  - no write to `businessDispatch` occurs on this path; latest runtime watcher still shows the only `businessDispatch -> 05017509` install happens during `sub_A50()` initialization of `010534b4`
- implication: the next expected response owner while this screen remains active is still `sub_938`, not `0x2D80`
- current best hypothesis is therefore that this screen expects a different reply subtype family after the `1/1/1` submit, most likely one of the `sub_938`-accepted `type=1/subtype=2|3|6` objects, rather than a direct primary-wrapper-owned `1/1/1` result packet
- current live mock experiment has been narrowed accordingly: the default non-`1234` `requestSubtype==1` success path now returns the existing actor-style `1/1/6` object so the next rerun can test whether `sub_938` begins mutating local title state when fed one of its accepted subtypes; the old `1/1/1` validation packet remains reachable with `CBE_LOGIN_RESPONSE=primary-validate` for control comparisons

Latest runtime result:

- status: `confirmed`
- the `1/1/6` experiment does advance this screen, but not by mutating the old `trace_title_login_state` fields in place
- on the first confirmed non-`1234` run, the wire payload really was `1/1/6`: `bin/logs/net_packets.log` raw dump begins `575401b001010106...`, even though the current packet-summary line still misleadingly prints `objs=1/1/1`
- after the queued `title-mode15` follow-up and the `1/1/6` data event, `010534b4` is removed (`screen_manager idx=6 remove r0=010534b4`), `0105a814` is unwound, and a new module screen `01053450` is created (`screen_manager idx=4 add r0=01053450`)
- the owner also changes: `businessDispatch` leaves `05017509` and is reinstalled as `01012e4d` on the new `01053450` screen
- scene/runtime state advances in the same window (`sceneState=7`, `loadFlags=1,0,0`), which never happened under the earlier `1/1/1` primary-validation replies
- the newer isolated control removes one remaining ambiguity: queued `1/1/15` is not the cause of that jump. With `request 1/1/1` follow-up queueing disabled (`skip_login_followup_event ... queueMode15=0`), a lone `1/1/6` success object still tears down `010534b4`, installs `01053450`, and immediately resumes the usual scene-bootstrap request sequence (`1/5/10 + 1/7/7 type=1`, then `1/7/7 type=2`, `1/7/7 type=3`)
- the subtype-2 comparison now shows the same control-flow result with a smaller business payload. An isolated `1/1/2` success object (`raw ... 01010102 ...`) still removes `010534b4`, installs `01053450`, and resumes the same scene-bootstrap request family, even though the payload drops the subtype-6-only fields and only carries `result, revivetype, ruffianflag, type, lastexp, curexp, persentexp, actorinfo`
- the subtype-3 comparison lands in the same place again. An isolated `1/1/3` success object (`raw ... 01010103 ...`) uses the same smaller field set and payload length as subtype `2`, and still removes `010534b4`, installs `01053450`, and resumes the same scene-bootstrap request family

Current combined implication:

- on the current emulator path, all three `sub_938`-accepted login-success siblings that have been tested in isolation (`1/1/2`, `1/1/3`, `1/1/6`) converge on the same control-flow result:
  - `010534b4` is removed
  - `businessDispatch` is reinstalled as `01012e4d`
  - `01053450` is created
  - the client enters the same scene/bootstrap sequence and reaches `sceneState=7` with `loadFlags=1,0,0`
- therefore the remaining mismatch with the expected pre-map title flow is no longer explained by choosing the wrong subtype among `2/3/6`
- the later `trace_title_router_gate` / `trace_title_flow_action` runs narrow the branch point further:
  - `sub_938` is not directly calling the router callback on the current path; by the time it checks the router gate, `routerFlag17=1` and `routerFlag18=1`, so the `router+44` callback is suppressed
  - the actual teardown is performed by the local `sub_5B0` mini-flow immediately afterwards
  - the decisive local condition is not a late network field but an already-armed object state: the `sub_5B0` object has `obj28=1`, `obj2C=1`, `local10340=1`, and `local10344=1` before the login success completes
  - once `sub_938` accepts the login object, the same local object's `byte2` becomes `3`; `sub_5B0` then hits `sub_5B0_remove_current_screen` and calls `sub_EC(3)`, which is the immediate precursor to `01053450`

Implication:

- the remaining title-stage mismatch is now most likely an upstream local-mode contract problem, not a wrong `1/1/x` success subtype among `2/3/6`
- the next RE target should be: which earlier local title/startup path arms `obj2C=1` and `local10344=1` on the `sub_5B0` object before a true role/manage workflow is entered, and what the expected alternative state should be on a real non-direct-enter login

Newer upstream arming result:

- status: `confirmed`
- that earlier arming path is now identified more narrowly: the `obj28=1` / `obj2C=1` transition is performed by `sub_938(event=5)` itself, not by a separate hidden writer
- static `sub_938()` case `5` does exactly three things in order:
  - writes `*(R9+0x283C+0x28) = 1`
  - writes `*(R9+0x283C+0x2C) = 1`
  - sends `net_build_login_request(99, 1, 0)`
- runtime matches that sequence on the active `010534b4` title screen:
  - at `tick=110`, a second `open_channel connect=2` queues `event=5`
  - at `tick=117`, that `event=5` fires and immediately sends a `len=9` `99/1` request
  - the emulator's current built-in answer is `source=builtin-short-63-1-echo`
  - the resulting `event=7` callback leaves the visible title snapshot unchanged, but the later `sub_5B0` traces already show the object staying in armed mode-1 state before normal account-login success arrives
- the latest dedicated rerun strengthens that from a static/runtime match into a behavior claim:
  - right before `sub_938_case5_arm_mode1`, the local object is still `obj28=0 obj2c=0 local10340=0 local10344=0`
  - immediately after the case-5 trace, the next `sub_5B0_load_mode` sample becomes `obj28=1 obj2c=1 local10340=1 local10344=1`
  - after the current `99/1` echo response is consumed, `trace_title_login_state` remains unchanged and `sub_5B0` keeps looping in mode `1` with `byte2=0`
  - therefore the current mock `99/1` reply is not neutral in effect; it is a confirmed insufficient response that leaves the later direct-enter preconditions armed
- implication:
  - the remaining mismatch is now best framed as a `99/1/0` title-stage contract question
  - the unknown is no longer who flips `obj2C`, but what the real `99/1` response is supposed to do next on this screen before accepted login success siblings (`2/3/6`) are allowed to set `byte2=3`
  - a simple transport close is now ruled down as the primary missing piece: enabling a queued `event=9` immediately after the current `0x63/1` echo does not clear `obj2C/local10344`, does not move the local `sub_5B0` object out of mode `1`, and does not change the later direct-enter outcome
  - a corrected owner/gate watcher run narrows the observed `99/1` side effect further: during the current `event=7` callback, runtime `0501760c` clears `objC.flag10` at `01056100`, and the paired global watcher confirms that same byte is `titleLoadingGate` (`R9+21808`)
  - that gate clear is then consumed locally by the loading widget: `loading_gif_widget_draw()` drops `obj8.flag10` to `0` at `010461c8`, which explains why the already-understood frame counter `obj8_stateA` freezes at `25`
  - importantly, this still does not disarm the title owner's armed mode-1 state; `ownerObj.obj28=1`, `ownerObj.obj2C=1`, `local10340=1`, and `local10344=1` all persist until later login success sets `ownerObj.byte2=3`
  - so the current emulator behavior for `99/1` is now best described as "clear titleLoadingGate / stop the loading widget" rather than "advance the title object into a non-direct-enter mode"; the remaining missing contract is whatever should also clear or reroute `obj2C/local10344` before `sub_5B0` reaches `sub_EC(3)`
  - static recovery now explains that write exactly: runtime `0501760c` is `sub_938+0x104` (`0xA3C`), the generic tail of `sub_938(event=7)`. After the event-7 packet scan completes, case `7` unconditionally clears the local child object at `*(R9+10312)` by writing `+12 = 0` and `+16 = 0`, then returns via the parent callback
  - the later login-success-side gate clear at runtime `050175cc` is the sibling write inside the accepted-object branch (`sub_938+0xCC` / static `0x9FC`): once `type=1/subtype=2|3|6` is accepted, `sub_938` clears that same `+16` byte before checking the router gate, and only then sets `R9+10302` (`ownerObj.byte2`) to `3` or `4`
  - therefore a pure `99/1` event on the current screen is not taking any hidden semantic branch inside `sub_938`; it simply fails the `type=1/subtype=2|3|6` filter and falls through to the generic case-7 cleanup, which is why it clears `titleLoadingGate` but leaves `obj2C/local10344` armed and `byte2` unchanged
  - `sub_5B0()` now gives a plausible concrete consequence for that cleanup. While `obj2C == 1`, its local mode-1 branch watches `*(_WORD *)(*(_DWORD *)(v0 + 8) + 10)` and, once that counter exceeds `300`, it queues `title_activate_role_manage_screen` via `sub_10C6(&unk_82C, title_activate_role_manage_screen)`. That same `+10` field is the traced `obj8_stateA` frame counter
  - `sub_722()` is the matched local disarm/reset path used by the existing role-list / role-manage screens: it zeroes the child counter, restores `*(R9+10312)+16 = 1`, and clears `10332/10336/10340/10344` back to `0`
- because the current `99/1` generic cleanup clears `titleLoadingGate`, the loading widget stops and `obj8_stateA` freezes at `25`, so the built-in `>300 -> title_activate_role_manage_screen` branch in `sub_5B0` never gets a chance to fire before later login success sets `byte2=3`
- this makes the current mismatch look less like “wrong accepted login subtype” and more like “the pre-login title-mode timer/transition is being cut short”: either the real `99/1` contract should keep the counter alive long enough to hit that branch, or some other companion packet/local callback should invoke the same role-manage/reset path directly
- delayed-success control has now refined that further. When the main accepted login-success `event=7` is held back by `360` ticks, the counter is not permanently stuck at `25`: on the latest rerun it resumes, reaches `obj8_stateA=301` at `tick=541`, and runtime logs the exact threshold-site callback `sub_5B0_queue_activate_role_manage`.
- the threshold side effects are now concrete too. In the same `tick=541` window, `obj8.stateA` resets `301 -> 0`, `obj8.flag10` clears `1 -> 0`, and `objC.flag10` / `titleLoadingGate` clears `1 -> 0`, but the owner itself stays armed (`byte2=0`, `obj28=1`, `obj2C=1`, `local10340=1`, `local10344=1`) and the delayed accepted login-success packet is still pending in the scheduler.
- implication: `sub_5B0`'s local timeout branch is real and is now runtime-confirmed, but the currently observed effect is only the child-widget reset plus a queued role-manage activator. The still-missing contract is whatever should execute after `sub_10C6(&unk_82C, title_activate_role_manage_screen)` is queued, because the current session shows no immediate screen replacement or owner-byte transition before the run ends.
- the callback-queue watcher now narrows that one level further. The active title path already commits a queue entry much earlier, during the first `99/1` exchange (`sub_1032_queue_enter` + `sub_1032_queue_commit` at `tick=110`), with `queueText=050177f0` and `queueCallback=0`.
- when `obj8_stateA` later crosses `300`, `sub_5B0_queue_activate_role_manage` does reach `sub_1032`, but only the entry trace appears; there is no second commit trace because `queueActive` is already `1` at entry. So the role-manage activator is currently blocked at queue admission time, before `title_activate_role_manage_screen()` itself ever runs.
- implication: the remaining contract gap is now specifically a stale title-local queue item. Either the earlier `99/1`-era queued item is supposed to clear long before the timeout branch fires, or the emulator is missing the consumer/state change that would release that queue and allow the later role-manage callback to be committed.
- static/runtime correlation now identifies that stale item more concretely. Runtime `queueText=050177f0` relocates back to static data `&unk_C20`, and `sub_938(event=7)` enqueues `sub_10C6(&unk_C20, 0)` exactly on the early branch where its packet helper `(*...+20)(R9+10320, a1, 10, 19)` succeeds. So the long-lived queue item is not a generic later popup; it is created directly by the current `99/1` handling path before any accepted login object is scanned.
- the widened rerun also rules down the obvious local clearers on the current path: after the initial `tick=110` enqueue, no later queue-field writes occur at all, and none of the candidate consumer/reset functions (`title_activate_popup_screen_if_idle`, `sub_F50`, `sub_FF0`, `sub_5922`, `sub_5A74`, role-manage menu/network handlers) execute before the delayed accepted login-success packet eventually forces `byte2=3`.
- implication: the stale-queue problem is currently upstream of those clear/reset helpers. Either the emulator is missing the local callback/timer that should revisit the `&unk_C20` queue item, or the `packet_check(...,10,19)` success branch in `sub_938` is being taken under conditions that should not occur on the intended non-direct-enter title path.
- the newest popup-driver rerun narrows that again: the timer/driver itself is not missing. After the initial `99/1` queue commit, `sub_59E_popup_dispatch` keeps re-entering on the active `010534b4` screen with `queueActive=1`, `queueText=050177f0`, `queueCallback=0`, `popupTarget=0`, and `popupCount=0`, so the stale queue state is being revisited by the local driver.
- the same rerun also rules out a simpler “late accepted login success recreated the popup item” explanation. At the delayed success delivery that later sets `ownerObj.byte2=3`, `sub_938_packetcheck_10_19_result` reports `r0=0`, which matches the static `if (packet_check(...,10,19)) sub_10C6(&unk_C20, 0)` branch shape and means that branch did not succeed on the accepted-object path.
- implication: the remaining contract gap is now below the generic popup timer. The unresolved step is whatever popup-object method or local condition should turn the already-revisited `&unk_C20` queue item into a real popup-screen activation or queue release before the later accepted login success tears the screen down.
- the new bridge watcher narrows that one layer further. On the active `010534b4` title screen, `sub_564()` really is driving the shared objC bridge: before the stale queue becomes active, the bridge still reaches `cb28_poll`, writes `objC.stateC=3`, calls `cb24_dispatch`, and can later enter `cb30_guard -> cb2C_pump` while the child event object reports `eventObj+16 != 0`.
- once `queueText=050177f0` / `queueActive=1` is committed, that richer bridge path disappears. The repeated revisit collapses to `cb30_guard` only, with bridge state holding at `field4=2`, `stateC=0`, and child event state `eventObj+16 == 0`. Static `sub_1034868()` explains why that matters: it only calls `sub_103478E()` when `*(_DWORD *)(eventObj + 16)` is non-zero, otherwise it returns immediately.
- implication: the remaining popup/queue mismatch is now best framed as a child-event rearm problem inside the shared bridge. The high-level queue is present and the timer is still revisiting it, but the bridge's child event object is no longer armed for the `cb2C_pump` path that would actually advance or recycle the queued popup work.
- the newest rerun identifies the concrete arm/disarm helpers too. `event_packet_add_field()` is what writes `eventObj+5 = 1` and increments `eventObj+16`, while the bridge pump path `sub_103478E()` later clears `eventObj+5` and immediately calls `event_packet_init()`, which resets `eventObj+16` back to `0`.
- a newer static pass sharpens the semantics of that child object: it is an outbound WT builder, not an inbound parser. `sub_103478E()` first calls `eventObj+40` (`event_packet_calc_size`) and `eventObj+32` (`event_packet_build_WT`), hands the resulting bytes to the net-manager bridge, and only then resets the object with `event_packet_init(..., 0, n10, n19)`. So the open question is specifically who should add the next outbound object after the stale `&unk_C20` queue item becomes live.
- the newest caller trace now answers part of that directly: the live `event_packet_add_field()` calls on this path come from `alloc_outgoing_game_event()` (`0x0100E2E4`, LR `0100E2F9`). On the latest delayed-success run, that caller appears once in the original pre-queue cycle and then not again until a later explicit local touch/submit path; the stale queue / popup-driver revisit loop does not call it by itself.
- implication: the interesting missing contract is no longer "who cleared `eventObj+16` later". The bridge itself clears it as part of its normal pump/reset cycle. The real gap is what should rearm that child event object again after `queueText=050177f0` has been committed, because on the current emulator path no later call re-populates `eventObj+16`, so `cb30_guard` never re-enters `cb2C_pump`.
- static bridge recovery now narrows that “who should rearm it” question further. `mmTitleMstarWqvga.cbm::sub_564()` is structurally the same as main-CBE `startup_maybe_start_async_net_task()` (`0x0103A77C`): both wait on a local `+0x24` pending field and `+0x20` counter, call shared-bridge `cb28`, then call scene-object slot `+0x15C`, and finally pass that return value into shared-bridge `cb24`.
- main-CBE `scene_object_vtable_init()` identifies slot `+0x15C` as `sub_1018DC6()`, which does not allocate outgoing events itself. It only loads the host string from `mmorpg_config` (or falls back to `jhol.51coolbar.com:20888`) and returns that pointer.
- combined implication: the periodic title/startup bridge that leads to `alloc_outgoing_game_event()` is now:
  - local async driver (`sub_564` / `startup_maybe_start_async_net_task`)
  - host lookup (`scene_object + 0x15C -> sub_1018DC6`)
  - shared-bridge `cb24_dispatch -> open_channel`
  - queued `event=5` callback
  - later `alloc_outgoing_game_event()` in the business request sender
- this means the next active question is not “which direct caller should magically jump to `alloc_outgoing_game_event()`”, but “why the next `cb24/open_channel/event=5` cycle does not restart once the stale `&unk_C20` queue item is live on the current title path”.
- the newest async-counter watcher answers one half of that. The bridge does successfully reach `cb24/open_channel` once on the active `010534b4` path: `ownerObj.asyncCounter` climbs until `31`, then `sub_564_before_cb24_dispatch` / `sub_564_after_cb24_dispatch` fire, `open_channel connect=2` is issued, and `ownerObj.asyncPending` becomes `1`.
- corrected branch attribution: the later absence of `sub_564()` is **not** because `sub_59E()` itself stops calling it. Static `sub_59E()` only checks `R9+10303` / `popupSkip` and otherwise always tail-calls `sub_5B0()`.
- static `sub_5B0()` is now the key:
  - `BL sub_564` appears only twice in the whole title module, at `0x5D2` and `0x68C`
  - `0x5D2` belongs to the `obj4.state8 > 0` branch
  - `0x68C` belongs to the idle `obj4.state8 <= 0 && obj2C == 0` branch
  - the `obj2C == 1` mode-1 branch at `0x6CA` never calls `sub_564()`
- runtime on the long delayed-success loop matches that exact `obj2C == 1` branch: after `sub_938(event=5)` arms mode-1, repeated `tick~781+` samples show `sub_59E_popup_dispatch -> sub_5B0_load_mode -> sub_5B0_mode1_branch -> shared_popup_bridge_cb30_guard -> sub_5B0_check_byte2`, with `obj28=1 obj2c=1 local10340=1 local10344=1 byte2=0`. So the missing second `event=5 -> alloc_outgoing_game_event()` cycle is not immediately caused by stale queue residency; it is caused by the owner staying in the `n2==1` mode-1 branch that bypasses both `sub_564()` callsites.
- `sub_840()` confirms the same split from the local-input side. Once `obj4.state8 <= 0`:
  - `local10344 == 1` / `obj2C == 1` gates on `obj8.flag10`; when that flag drops to `0`, it dispatches through callback slots `R9+10452` / `R9+10456`
  - `local10344 == 2` / `obj2C == 2` routes local input into `sub_74A()` (`n3 == 3`) or directly into `title_activate_role_manage_screen()` (`n3 == 0 && *a3 == 0x2000`)
- `sub_74A()` is the first confirmed local branch that can either disarm or continue deeper into role-manage:
  - hit test `(10,359)` -> `sub_722()` full reset
  - hit test `(181,359)` -> `title_activate_role_manage_screen() -> sub_EC(9)`
- `sub_722()` is now the only confirmed full disarm/reset helper for this local state. It clears owner `+0x24/+0x28/+0x20/+0x2C`, resets the child timer, and re-enables `objC.flag10`; its current callers are `sub_74A()`, `role_list_screen_handle_action()`, and `role_manage_screen_handle_role_list_nav()`, i.e. role-list / role-manage local flows rather than the current `99/1` path.
- the newest runtime watcher resolves the next ambiguity around mode-1 local input:
  - the callback slots are already populated during early screen setup: `ownerObj.mode1CbA = 01048F8D`, `ownerObj.mode1CbB = 01048FB3`
  - once `obj2C == 1` and `obj8.flag10 == 0`, `sub_840()` really does call the first slot repeatedly (`sub_840_mode1_call_cbA`); this is not a missing-init case
  - the second slot (`sub_840_mode1_call_cbB`) is not exercised on the current run, and runtime never reaches the `obj2C == 2` input exits that would call `sub_74A()` or `title_activate_role_manage_screen()`
- static recovery of the main-CBE targets shows that the exercised slot is only a thin adapter family:
  - `01048F8C` checks an object pointer plus `sub_1015F40(n2)` and, when allowed, dispatches to the pointed object's `+4` method
  - `01048FB2/01048FB4` is the sibling wrapper for the pointed object's `+8` method with two arguments
- updated implication: the current blocker is no longer "mode-1 callbacks missing". The owner is already armed, the first mode-1 callback is already running, and it still does not clear `obj2C/local10344`. The next narrow target is the concrete object behind that indirection and its `+4/+8` methods.
- updated implication: the active contract gap is no longer "why stale queue suppresses `sub_564()`". It is "what real title/local event is supposed to clear `obj2C/local10344` or otherwise invoke the `sub_722()`-style disarm after `sub_938(event=5)` arms mode-1".
- that concrete-object check is now partly resolved. The live `modeObjCb4/8/C` values behind `ownerObj.modeObjPtr` map back to two normal title UI object families in `mmTitleMstarWqvga.cbm`:
  - `050192f5/05019399/050193fb` -> relocated `0x2724/0x27C8/0x282A` -> `title_four_choice_screen_handle_action`, `title_render_four_choice_menu_hitboxes`, `title_four_choice_screen_render`
  - `050196ef/050197b1/05019867` -> relocated `0x2B1E/0x2BE0/0x2C96` -> `login_form_handle_action`, `login_form_handle_touch`, `login_form_render`
- runtime also shows the owner-side pointer switching between those families: `ownerObj.modeObjPtr` is first written to `0501f820` during setup, then changes to `05010db0` at `tick=123` while `obj2C=1` / `local10344=1` are already armed.
- the direct caller trace now resolves who performs those installs:
  - the first `modeObjPtr` set comes from relocated static `0x0B7B` inside `sub_A50()` screen init
  - the later `0501f820 -> 05010db0` swap comes from relocated static `0x27BB` inside `title_four_choice_screen_handle_action()`
- static `title_four_choice_screen_handle_action()` explains that second transition exactly. On confirm/center input, it uses the wrapper at `v1 + 10464` to install one of two objects: selection `1` loads the pointer stored at `v1 + 10792`, selection `2` loads the sibling pointer at `v1 + 10796`, selection `0` sends `net_build_login_request(1, 12, 5)`, and selection `3` exits via `sub_EC(9)`.
- the sibling slots are now runtime-classified too:
  - `choiceObj1 = 05010db0` and carries `050196ef/050197b1/05019867` -> `login_form_handle_action`, `login_form_handle_touch`, `login_form_render`
  - `choiceObj2 = 0501f808` and carries `0501c0a3/0501c127/0501c181` -> relocated `0x54D2/0x5556/0x55B0`
  - a later forced-install runtime experiment now shows this object renders the recharge-selection page (`江湖充值`, `快速充值`, `手动充值`, `对登录的默认账号直接充值`, `确认`, `返回`), so the earlier "old login screen" label should be treated as superseded by direct screen evidence
- implication: the four-choice screen is branching between login-form UI and a recharge-selection UI, not between login and a hidden role/popup transition object.
- implication: the repeated `sub_840_mode1_call_cbA` path is dispatching into ordinary four-choice/login-form UI handlers, not into a hidden role-transition or reset object. So the missing disarm path remains upstream of the exercised mode-1 callback adapter family.
- updated next narrow target: stop treating either sibling as the likely missing role-manage branch. The remaining question is which local state chooses between those two login UIs, and what later real event/path after either one should replace the active login object family or clear `obj2C/local10344` before accepted login success can arrive without forcing the `byte2=3 -> sub_EC(3)` direct-enter path.
- the later direct-jump validation now anchors the already recovered `target50` sibling more firmly. Forcing the normal `choiceObj1` install site to substitute `target50` (`base+10808`) produces `newPtr=0501f850` with callback triple:
  - `0501ae61 -> role_manage_screen_dispatch_mode_input()`
  - `0501ae89 -> role_manage_screen_handle_input()`
  - `0501b2c5 -> role_manage_screen_render()`
- the immediate `mode -> 0`, `sel0 -> 0`, `sel1 -> 0` writes seen after that substitution are not evidence of a special fallback by themselves. They relocate to `role_manage_screen_init()` (`0x3AE8/0x3AF4/0x3AFE`), i.e. the role-manage family performs those resets as part of its normal startup.
- implication: the `target50` direct-jump experiment proves the role-manage object family is real and installable from the current title screen, but it still enters its standard zeroed startup state with `listPtr=0` / `roleFamily=0`. The remaining gap is therefore the backing role/server data or pre-init state that should exist before this family can progress.
- the newer `target4c` differential experiment now validates the upstream family directly: forcing the same install site to substitute `target4c = 0501f838` immediately enters `role_list_screen_init()` and emits a real outbound `1/1/16` packet (`WT len=9`, `hex=575400090101100005`).
- implication: the active missing contract has moved from “which local target enters role-list” to “what the emulator/server should return for that live `1/1/16` request so the client can later reach `title_handle_role_list_response()` / `1/1/4` and seed `base+10788` before `target50` is usable”.
- current minimal live-response experiment now mirrors that recovered contract directly: the built-in mock answers live `1/1/16` with a two-object WT packet containing `1/1/16 { result = 1 }` plus `1/1/4 { actorinfo = counted title-side role-entry table }`.
- rationale: `title_handle_role_list_response()` first caches subtype-16 `result` into ready byte `+11046`, then, while that ready byte is `1`, optionally reads `servconf` and parses subtype-4 `actorinfo`. This makes `16(result=1) + 4(actorinfo)` the narrowest confirmed response shape worth validating before guessing at richer fields.
- runtime now confirms that this minimal shape is accepted by the real title-local parser: the live trace reaches `title_handle_role_list_response()` twice on the same `event=7`, then `title_transition_router_invoke_default_target()`, with `ready=1`, `listPtr` seeded, and `roleCount=1` at router time.
- updated implication: the remaining gap is no longer the `1/1/16` response contract itself. It is the later local handoff after that router callback, because the screen still does not settle into a stable role-manage state and later target50-side traces still show zeroed role counters.
- corrected by the newest static+runtime pass: `target50`'s observed `mode=0` / `roleFamily=0` should no longer be read as a generic failure or uninitialized state.
  - `role_manage_screen_dispatch_mode_input()` (`0x4290`) uses `mode` as a real submode selector:
    - `0` -> `role_manage_screen_handle_role_list_nav()`
    - `1` -> `role_manage_screen_handle_action_menu_input()`
    - `2` -> `role_manage_screen_handle_create_name_input()`
  - `role_manage_screen_handle_input()` (`0x42B8`) uses `roleFamily` as a live three-way family/tab selector. Touching a different family writes `roleFamily = 0/1/2`, while keeping the same family dispatches confirm-style local inputs (`0x4000` / `4096`) into the active submode.
  - when `roleFamily == 0` or `roleFamily == 1`, `role_manage_screen_handle_input()` also enables extra left/right hotspot handling for that family.
- implication: by first `target50_render`, the current good chain is already inside the normal role-manage role-list navigation submode (`mode=0`) on family/tab `0`, with seeded workspace and valid `ready/initDone/slotLimit`. The remaining mismatch is therefore more likely the semantic contents of the role workspace or a later visual/input expectation than the mere zero values of `mode` and `roleFamily`.
- the newest role-record trace now shows that the synthetic workspace is mostly plausible but still incomplete:
  - `record+0x68` (`role0.id`) = `10001`
  - `record+0x48` (`role0.name`) = `侠剑江湖`
  - `record+0x144` / `record+0x145` (`role0.sex` / `role0.job`) = `0 / 1`
  - `record+0xB0` (`role0.level`) still samples as `0`
  - workspace `+2044` (`selectedRoleIdShadow`) still samples as `0`
- the dedicated write watcher now proves those are not late render-side losses:
  - the workspace is first cleared wholesale by local code (`0501c770/0501c774`)
  - then `title_handle_role_list_response()` repopulates `roleCount`, `role0.id`, `role0.sex`, `role0.job`, and `role0.name`
  - the same parser path also writes `record+0xB0`, but writes `0` there (`last=0501a294`)
  - `selectedRoleIdShadow` is never repopulated after the earlier clear
- the corrected reader-method trace now resolves why:
  - `record+0x68` comes from `stream_read_i32_be_tagged`
  - `record+0x144` / `record+0x145` come from two `stream_read_i8_tagged` calls
  - the name at `record+0x48` comes from `stream_peek_i16_be` + `stream_read_cstr_len16`
  - `record+0xB0` comes from `stream_read_i16_be_tagged`
- implication: the title-side compact role-entry tail is `(tagged i32, tagged i8, tagged i8, len16+cstr, tagged i16)`, not `(tagged i32, tagged i8, tagged i8, len16+cstr, tagged u32)`.
- the subsequent rerun now confirms the builder fix: `record+0xB0` is repopulated as `1`, and `target50` samples the first role record as `id=10001, lvl=1, sex=0, job=1, name=侠剑江湖`.
- a later crash analysis narrows one more field semantic: `target50_render()` derives a portrait/render callback index as `job * 2 + (sex - 1)` before its final indirect call. This means the compact title-side role-list `sex` field is 1-based for this contract, not 0-based. Feeding `sex=0` underflows the local index and reaches the invalid callback pointer `0x0001FFF0` (`lastPc=0x0501B794`, `lr=0x0501B797`).
- the current built-in title role-list builder now clamps that compact `sex` field to `1..2`; the earlier `0..1` encoding was only valid for the later in-scene actor family and is not safe for `target50` role-manage rendering.
- `selectedRoleIdShadow` still remains `0` at first render, but static `role_manage_screen_select_role_slot()` explains that this is not part of the `1/1/4` parser contract. It is only written later, on local confirm of a non-final role slot, right before the live `net_build_login_request(1, 6, 1)` role-select request.
- the next live title packet after confirming a normal role slot is now confirmed on the wire:
  - outbound top-level WT header: `1/6`
  - single object header: `1/1/6`
  - current confirmed field payload: `actorID` (`u32`, selected role id)
- static `role_manage_screen_handle_network()` treats incoming subtype `6` as the post-confirm role-select response family. Its current case-6 branch does not query any response fields before persisting `mmorpg_LoginRecord`, so the first emulator-side validation now uses a deliberately minimal `1/1/6` response object (`result=1`, echoed `actorID`) to see whether the client naturally advances into later scene/bootstrap traffic.
- runtime now confirms that this minimal `1/1/6` role-select ack is accepted by the real `target50` family: after local confirm writes `selectedRoleIdShadow` and switches to `mode=3`, the next `event=7` forwards `packetSubtype=6` into `role_manage_screen_handle_network()`.
- updated implication: “missing `1/6/1` handler” is no longer the active blocker. The current single-object `1/1/6` ack is enough to reach `target50_handle_network(case 6)`, but it does not by itself produce any observed local transition or later scene/bootstrap request on the latest rerun. The remaining gap is therefore more likely an additional companion object or follow-up packet after subtype `6`, not the existence of subtype `6` itself.
- the current next-step experiment now promotes the nearest confirmed sibling branch into that companion role: live role-select replies are answered as a two-object combo `1/1/6 { result=1, actorID=<echo> } + 1/1/15 { result=1 }`, because `case 15` is the closest title-local branch that provably performs an immediate local mode transition.
- that combo exposed a scene-side contract mismatch: after subtype-15 advances the client, `scene_runtime_init_and_sync()` also consumes the same subtype-6 object as part of its cached scene-enter packet loop. Static recovery shows this path treats `type=1/subtype=6` as a full scene-enter family packet and reads `revivetype`, `ruffianflag`, `type`, `practiseflag`, `pcimg`, `expcard`, `expbook`, `practiseinfo`, `lastexp`, `curexp`, `persentexp`, and later actor/blob data. A bare `{result, actorID}` ack therefore crashes in `parse_actorinfo_response()` (`pc=0x01033A68`, `lr=0x0100FAC5`) once that bootstrap path is taken.
- current fix: `builtin-title-role-select` now sends the full subtype-6 success shell first, then appends `1/1/15 { result=1 }`. This keeps the local `case 15` action-menu experiment while satisfying the stricter scene-side subtype-6 consumer well enough to avoid the known null-blob fault.
- implication: the remaining mismatch is now more specifically inside the seeded `1/1/4.actorinfo` role-record semantics, not in the outer `1/1/1 -> 1/1/16 -> 1/1/4` packet sequence.
- that selection source is now partly resolved too. The watched four-choice byte `choiceSel` (`base+10733`) starts at `0`, stays `0` across the early `99/1` window, and only later flips `0 -> 1` at runtime `05019098`.
- static `05019098 -> 0x24C8` maps to `sub_248E()`, the generic local hit-test helper used by the four-choice screen. At that exact site `sub_248E()` writes `*a9 = n2_2` when the current pointer/touch hits option `n2_2` and the existing selection differs; if the hit option already matches, it instead calls the supplied callback with `0x4000`.
- implication: the current `four-choice -> choiceObj1/login_form` path is a normal two-step local UI sequence rather than a hidden forced branch:
  - first hit: `sub_248E()` selects option `1`
  - second same-hit/confirm: `sub_248E()` calls `title_four_choice_screen_handle_action(0x4000)`, which installs `choiceObj1`
- updated next target: the option-2 differential check is now complete enough to change direction. Forced selection `2` proves the recharge-selection UI still runs inside the same armed `obj2C/local10344=1` mode-1 loop, so the next useful question is no longer "what is choiceObj2" but "what later real event/path after either login-form or recharge-selection should disarm mode-1 before accepted login success arrives".

Implication:

- for the current stuck title path, `type=1/subtype=6` is no longer just a plausible alternative; it is the first confirmed post-submit success family that advances the local screen stack
- the login investigation should now shift from `010534b4/sub_938` to the new `01053450` screen and its `businessDispatch=01012e4d` owner

### Shared business/event owner at `R9+0x94A4`

Status: `confirmed`

The queued `event=7` transport wrapper does not own its own high-level protocol family. It forwards into the object currently stored at `*(R9+0x94A4)`.

Confirmed object constructor:

- `0x01034922` initializes the object fields and installs callbacks:
  - `+0x24 = sub_10348FC`
  - `+0x28 = sub_1034874`
  - `+0x2C = sub_103478E`
  - `+0x30 = sub_1034868`
  - then stores the object to `*(R9+0x94A4)`

Confirmed current caller:

- the only direct caller of `0x01034922` is `0x01003856 -> scene_system_bootstrap`

Implication:

- on the current live path, the shared business/event owner at `R9+0x94A4` is already a scene/bootstrap-owned object before login completes
- therefore queued login replies are not being delivered into a title-login-owned high-level parser; they are first routed through a scene/bootstrap owner
- this matches runtime evidence where `sceneObj` is already live and `cb44=01037473` remains stable while the title login screen `010534b4` is still active
- the newest rerun strengthens this from “already scene-owned during login” to “installed once and never replaced”: `trace_scene_owner_site` fires at `tick=0` for `scene_system_bootstrap` and then `shared_event_owner_init`, followed immediately by the only observed `trace_shared_event_owner_write slot=0105a078 old=00000000 new=010560f0`. No later title-stage overwrite of that slot appears before or during the `01056204 -> 0105A814 -> 010534B4` chain, nor during subsequent `1/1/1` / `1/1/15` deliveries.

Related local startup gate:

- `sub_1002538()` does not wait for any network response. It fetches a function pointer from `sub_1003C5A()`, and that helper simply returns `scene_system_bootstrap`; `sub_1002538()` then calls it directly and marks local byte `R9+21300 = 1`.
- `sub_100254A()` enters that direct branch whenever `sub_100AFCA() == 1`.
- `sub_100AFCA()` defaults to `1` unless a callback table at `R9+19848` exists, accepts `(1002,1)`, and reports a nonzero value for `(1002,3)`.
- `sub_100D04A()` also converges back to `sub_1002538()` on several local fallback conditions.
- runtime has now confirmed the first half of that contract: on the current emulator path, the `(1002,1)` callback is actually invoked via `0x0C003000` and returns `0`, after which `sub_100AFCA()` immediately takes its default-true path and enters `sub_1002538()`. The second `(1002,3)` query is therefore never reached on this path.
- address attribution correction: `0x0C003000` is the first `BILLING` manager stub (`VM_MANAGER_BILLING_FUNC_LIST_ADDRESS`), not a `DL_PAY` stub. So the local startup gate is currently being decided by emulator `BILLING` semantics, specifically the first billing callback entry that the CBE-side `vmdlEnterPay.c` logic uses for code `1002`.
- further correction from the newest rerun: `sub_100AFCA()` returns `true` only when `(1002,3) == 0`, not when it is nonzero. After changing the emulator billing stub so `(1002,1)` and `(1002,3)` both return `1`, runtime still falls into `sub_1002538()`, but now through the later `sub_100D04A()` fallback rather than through `sub_100AFCA()` itself.
- static decompile of `sub_100D04A()` narrows that later fallback to:
  - `sub_100B95C() == 1`
  - or `sub_1000648() == 0`
  - (`sub_1000202()` is hardcoded to return `1` and is therefore not the deciding branch on the current binary)

Implication:

- the current “title login screen plus scene-owned shared event router” state is consistent with a local startup mode gate that is already selecting the direct-enter/game bootstrap path before the title login flow finishes, rather than with a login reply that later misroutes control.
- more narrowly, the currently implemented emulator `BILLING` semantics are sufficient to trigger that direct-enter path by themselves.

Current scene-strip note:

- after the accepted 7-object scene follow-up, later manual touch activity still does not emit a newer outbound WT packet
- the visible center/bottom strip therefore remains a local UI/runtime question first, not a newly identified protocol gap
- the current runtime build adds only read-only local draw-owner traces for that strip (`trace_progress_strip_shape` / `trace_progress_strip_draw`); no new wire contract is inferred from the strip yet
- the first rerun with those traces confirms the point more strongly: the earliest hits all come from startup/loading helper `sub_100337C()` tiling a `36x36` image into an off-screen `240x400` buffer (`last=0100413e/lr=01004141`), so that batch is local overlay rendering noise rather than evidence for any later scene-side packet wait
- the newest rerun refines that again: the same helper is re-entered from `scene_rebuild_runtime_nodes()` during live scene bootstrap at `tick=182` with `sceneTickGate=1,1`, and then no further strip-region writes appear after steady `loadFlags=0,0,0` scene ticks resume. So the visible strip now looks like a stale locally redrawn bootstrap overlay, not a missing WT response
- earlier builds tried a local blocker-removal shim for the stale overlay draw at `sub_1003568()` and selector guards around `scene_draw_node_overhead_overlay()`. These are now removed because they bypass guest draw/init logic. The evidence still identifies the local scene/UI contract gaps, but the fix must come from the upstream resource/init/server behavior that should dismiss the overlay and populate selector state.
- latest steady-scene evidence also refines the current `1/2/10` interpretation: the self actor is being published into `sceneCurrentNode` successfully, but with bad spatial state. `trace_scene_current_node_publish` at `scene_rebuild_runtime_nodes()` shows `current=0x05400000 actor=10001 occupied=1 drawAt/step!=0`, while the same node carries `grid=50,50` and `target=0,0`. So the active issue is no longer “self actor missing from the wire payload”; it is “the current published node has wrong runtime coordinates/visual state”.
- newest static decompile of `parse_actorinfo_response()` at `0x0100FA88` sharpens one part of that spatial mapping:
  - after `primaryDisplayMax` and `secondaryDisplayMax`, the fresh-enter actorinfo path reads two `u8` values and stores them into the snapshot actor structure at `+0x11E/+0x120`
  - those slots are the same ones later reported as `target=%u,%u` by `trace_scene_current_node_publish`
  - the current mock builder therefore now treats them as target-position seed bytes rather than generic unknown fields, defaulting from `CBE_ACTOR_TARGET_X/Y` while preserving the older `CBE_ACTOR_BYTE_11E/120` override names
- working hypothesis for the remaining `grid=50,50` mismatch:
  - the final trailing `i16/i16` pair in actorinfo still looks like a grid/pose-like seed family rather than a motion-resource-only payload
  - this is not fully confirmed yet, but it is currently the only actorinfo default pair that still matched the old published `grid=50,50`
  - the mock now defaults that trailing pair from `CBE_ACTOR_GRID_X/Y` instead of a separate hardcoded `50/50` baseline so the next rerun can confirm whether the published current-node grid follows it
- newest static scene-runtime evidence also corrects one earlier visual-byte assumption:
  - `scene_runtime_init_and_sync()` builds six portrait widgets, then selects `sceneHudCurrentPortraitWidget` with `portraitBase + 44 * (visualGroup + 2*visualVariant - 1)`
  - therefore the scene-facing visual bytes are not a fully 0-based `(sex,job)` pair
  - current best fit is:
    - `visualVariant = job - 1` (`0..2`)
    - `visualGroup = sex + 1` (`1..2`)
- the current mock now uses that mapping for scene-facing actor seeds (`actorinfo` and `1/2/10 otherinfo`) while leaving the title/role-list `sex=0/1` contract unchanged
- latest runtime/static correlation narrows the remaining main-actor rendering issue away from the actorinfo wire blob itself:
  - after the visual-byte correction, `trace_scene_current_node_publish` now shows healthy `grid=12,10 target=12,10 visual=1,0`
  - the remaining bad field is `visualRes=0/0`
  - static `scene_rebuild_runtime_nodes()` binds the six main actor-family `.actor` resources through `scene_actor_asset_slot_table_bind_entry()`, then immediately calls `scene_node_refresh_visual()`
  - static `scene_node_refresh_visual()` does not parse new wire data; it simply indexes a six-entry per-node table at the start of the node using `visualGroup + 2*visualVariant - 1` and copies that entry into `node+0x24`
- current best fit for the “零碎人物图像” symptom:
  - the actor-response payload is already good enough to publish the right node coordinates and visual family
  - the remaining defect is more likely a local deferred actor-asset bind timing gap, where the selected node-table entry is still zero when `scene_node_refresh_visual()` first runs
- latest rerun reinforces that this is now a local runtime issue rather than a newly found wire-layout miss:
  - `trace_scene_current_node_publish` still shows healthy scene-facing actor seeds (`grid=12,10 target=12,10 visual=1,0`)
  - yet `visualRes=0/0` remains unresolved
  - `actor_pass` definitely executes, but the current `visualRes` fixup emits neither `trace_scene_visual_res_fixup` nor `trace_scene_visual_res_still_missing`
- implication for protocol work:
  - do not currently expand the `actorinfo` wire blob again just to chase the fragmented actor sprite
  - first resolve which local `actor_pass` early-return path is preventing the existing six-slot visual-resource check from running to completion
- latest rerun resolves that local sub-question too:
  - `trace_scene_visual_res_probe` now stays on `reason=ready`
  - by stable `actor_pass`, the current node already has `visualRes=0x054045A8`, and the selected slot-table entry matches it
  - `trace_sub_1010228_callsite` in the same tick also copies `field36=054045A8` into the scene-side scratch/render object
- implication for protocol work:
  - the fragmented on-screen actor is no longer good evidence for a missing actorinfo or `1/2/10` wire field
  - protocol work should stay frozen here while the local scene draw-path/state issue is traced further
- latest static runtime evidence makes that local split concrete:
  - `scene_draw_actor_pass()` has an early move-entry body-draw loop and a later per-node callback loop
  - the current self-node callback pair (`drawAt=0x01045579`, `step=0x01045429`) belongs to the per-node family, and `0x01045578` is `scene_draw_node_overhead_overlay()`
  - so the still-broken visible actor may now be a mismatch between the true body/world draw branch and the separate overhead/title branch, not a wire-level resource omission
- latest rerun confirms that split with runtime evidence:
  - the self actor's body/world draw branch is active (`trace_scene_body_draw_dispatch label=type2_body`)
  - but it computes off-screen coordinates `screen=-221,-479` from the live move-entry box fields `203,402,240,422`
  - meanwhile the separate current-node per-node callback family still targets `scene_draw_node_overhead_overlay()` at `screen=0,-67`
- implication for protocol work:
  - the visible fragmented actor is now stronger evidence for a local move-entry/world-to-screen state bug than for a missing actor wire field
  - separate from that, a new post-scene interaction request family now needs mock coverage:
    - `WT len=61 hdr=2/10 objs=1/2/10,1/14/14,1/14/4,1/14/5,1/14/6`
- current mock coverage update:
  - status: `confirmed` for dispatch boundary, `hypothesis` for full `14/*` semantics
  - IDA `net_business_response_dispatch()` (`0x01012E4C`) dispatches server response kinds `2..7`, `0x0A`, `0x14..0x1E`; it has no `kind=14` response consumer
  - therefore `src/main.c` now handles this exact `len=61` request with a narrow built-in `1/2/10 otherinfo` response only, reusing the confirmed compact self-node tuple consumed by `net_handle_actor_move_info()` case `10`
  - the `1/14/14,1/14/4,1/14/5,1/14/6` request objects remain protocol evidence for future request-side semantics rather than fields we should echo as server response objects
  - first manual rerun after adding this mock did not trigger the `len=61` family again: `net_packets.log` stopped at the accepted `WT len=49` scene-resource follow-up, while `net_trace.log` continued through scene `draw_pass/status_panels/actor_pass` ticks and contained no `unhandled_packet`, `assert(0)`, or `builtin-scene-interaction-followup` marker
  - second manual rerun produced the same protocol result: a fresh session reached `source=builtin-scene-resource-followup` at tick `143` and then continued local scene ticks, with no outbound `WT len=61` and no new unhandled/assert marker in `bin/logs/*.log`
- corrected interpretation of the existing `1/2/10 otherinfo` family:
  - static decompile of `sub_100F094()` shows that this blob is not a compact `(actorId, visual, name, target, ...)` tuple
  - it is a typed scene-record stream:
    - `short recordCount`
    - repeated `short actorTag, short posX, short posY, short propertyCount`
    - then per-property typed items, where the first short is a value-type discriminator (`2` = short, `3` = string in the currently confirmed cases)
  - only `actorTag == 2` or `actorTag == 9` appends a 32-byte body/world `ActorMoveEntryEx` into the move-entry table used by `scene_draw_actor_pass()`
  - `actorTag == 4` does not create a move entry; it only updates side-state such as `currentActorId`
- runtime evidence that forced the rewrite:
  - the older ad hoc `otherinfo` mock was being misparsed as one tag-2 move entry for `actorId=1`
  - `trace_actor_move_entry_table` showed `count=1`
  - `trace_actor_move_entry item=0` stayed on the bogus entry while the separately published `sceneCurrentNode` still belonged to actor `10001`
- current mock contract in `src/main.c`:
  - correction: the previous attempt to reinterpret `1/2/10 otherinfo` as a `sub_100F094()` record stream was disproven by static decompile
  - `net_handle_actor_move_info()` case 10 calls `sub_1012958()`, not `sub_100F094()`
  - `sub_1012958()` reads `otherinfo` as a compact self-node tuple:
    - `u32 actorId`
    - `u8 visualVariant`
    - `u8 visualGroup`
    - `string labelText`
    - `u8 targetPosX`
    - `u8 targetPosY`
    - `string shortLabel`
    - `string longLabel`
    - `i16 gridPosX`
    - `i16 gridPosY`
  - it then feeds those values straight into `scene_node_find_or_create(...)`
  - so `1/2/10 otherinfo` belongs to the scene-node publish family, not the body/world move-entry table family
- runtime consequence of that correction:
  - even when the temporary record-stream experiment was active and logged `otherinfo=records2(tags2+4 actorId=10001)`, the move-entry table was already built earlier in the same run and stayed unchanged
  - therefore the real producer of the body/world move-entry table is some source other than `1/2/10 otherinfo`
- newest runtime/static correlation narrows that producer:
  - `sub_100F094()` now logs `lr=0x0100DB3B`, which places the call inside `parse_actor_motion_descriptor()`
  - the same invocation uses `stream=0x0501F640`, matching the queued `mock_response ptr` of the bundled `builtin-group-type1` response in that tick
  - so the currently wrong body/world move-entry is being generated while parsing the earlier bundled scene-enter/group response, before the later `scene-resource-followup` packet family is processed
- newest rerun makes that producer concrete:
  - `parse_actor_motion_descriptor()` is entered with descriptor name `c00蓬莱仙岛_01`
  - its callback argument `a8` is `0x0100F095` (`sub_100F094+1`)
  - therefore the body/world move-entry table is currently being built by the map-descriptor path for the active scene key itself, not by `1/2/10 otherinfo`
- newest static/tooling correlation tightens the payload mismatch:
  - `tools/inspect_sce.py` parses `bin/JHOnlineData/c00蓬莱仙岛_01.sce` as one prop-scatter block plus two portals, with no ordinary actor records
  - the first portal has `spawn_point=(223,382)` and `trigger_rect=(203,402,240,422)`
  - those numbers exactly match the currently logged bogus move-entry words from `trace_actor_move_entry_append` / `trace_actor_move_entry item=0`
  - strongest current reading: the scene descriptor callback path is leaking portal/transition geometry into `sub_100F094()`'s body/world-entry consumer, rather than yielding a self-actor body record
- newest runtime handoff trace upgrades that from inference to direct evidence:
  - `trace_actor_motion_callback_handoff` at `0x0100DB2C` logs `cursor=135` and `tailShorts=3,2,223,382,8,1,5,1`
  - `trace_actor_move_entry_parser_entry` immediately follows with the same `stream/cursor`
  - therefore `sub_100F094()` is starting directly on the raw `.sce` portal token stream:
    - `3` = top-level portal record kind, misread as actor-record count
    - `2` = point token type, misread as actorTag
    - `223,382` = portal spawn point, misread as grid/point fields
    - `8,1,5,1,...` = portal meta8 payload, misread as property grammar
- runtime-guard status for this collision:
  - status: `removed`
  - superseded attempts: one guard skipped the whole callback at `parse_actor_motion_descriptor()` `0x0100DB32`; another skipped only the append-count write at `sub_100F094()` `0x0100F468`
  - reason for removal: these guards changed guest control flow/table publication to force progress, which violates the project rule that the game should advance through correct platform/server/resource contracts rather than emulator-side parser/draw bypasses
  - current direction: keep the portal-tail evidence above as a confirmed resource/parser mismatch, but fix it by identifying the upstream scene/resource/callback contract that should prevent `sub_100F094()` from consuming portal tokens as actor records
- static caller-chain follow-up narrows where that bad pairing is introduced:
  - `scene_hud_main_panel_init()` (`0x010352AE`) simply forwards its stacked extras into the generic initializer at `*(R9+0x5C58)+0x10`
  - `scene_rebuild_runtime_nodes()` (`0x0100F8B8..0x0100F8F6`) is the caller that first copies `currentMapIdText` into `parserNodeOrScenePtr->currentName`, then invokes `scene_hud_main_panel_init()` with stacked extras `{ currentNamePtr, 10, sub_100F094+1 }`
  - so the current live misroute is best described as:
    - scene-key/currentMapIdText source
    - paired by caller with `sub_100F094+1`
    - forwarded through `scene_hud_main_panel_init()`
    - parsed as `c00蓬莱仙岛_01.sce`
    - callback starts on portal tokens and appends the bogus type-2 body entry
- newest static source-trace closes the upstream field identity:
  - `scene_runtime_init_and_sync()` uses `R9+0x5E46` as `currentMapIdText`
  - that slot is the same persistent scene-key buffer already populated from the trailing `actorinfo` scene field
  - the function validates/normalizes `R9+0x5E46` via `sub_100EEBC()` / `sub_100FA40()`, then passes it straight into `scene_rebuild_runtime_nodes(..., currentMapIdText)`
  - therefore the current bad pairing is specifically “actorinfo trailing scene-key field + sub_100F094 callback”, not some unrelated temporary UI label buffer
- full actorinfo inventory check now limits the “maybe there is another hidden string field” branch:
  - fresh-enter parsing accounts for all currently known strings as:
    - role/name string
    - bounded `label(+0x6C)`
    - bounded `shortLabel(+0x100)`
    - bounded `previewImage(+0x10A)`
    - bounded `actorResource(+0xD8)`
    - trailing global `sceneKey(R9+0x5E46)`
  - current runtime snapshots already show coherent values in all of those slots (`JHOnline`, `10001`, `h_warriorwalk1.gif`, `h_warrior.actor`, `c00蓬莱仙岛_01`)
  - so there is no obvious remaining actorinfo string field that naturally fits the missing motion/body-descriptor role
- deeper static follow-up on the `scene_hud_main_panel_init()` family sharpens the local contract:
  - `scene_hud_main_panel_init()` (`0x010352AE`) forwards a four-word extra tuple into the shared initializer at `R9+0x5C58`, not a loose three-word blob
  - exact repacked layout at `0x010352B4..0x010352D4` is:
    - extra0 = caller `arg0` (`namePtr`)
    - extra1 = caller `arg4` (`mode`)
    - extra2 = hardcoded `0`
    - extra3 = caller `arg8` (`optional callback`)
- this matters because `mode=10` is now statically confirmed in two distinct panel-init paths:
  - fresh scene-enter path in `scene_rebuild_runtime_nodes()` uses `{ currentMapIdText, 10, 0, sub_100F094+1 }`
  - later `sub_100F094()` panel-refresh path at `0x0100F702` uses the same `mode=10`, but with callback slot `0`
- implication:
  - the current misroute is no longer best summarized as “mode 10 must be wrong”
  - stronger reading is “fresh scene-enter is pairing the actorinfo-derived scene key with a non-null `sub_100F094` callback on a mode-10 panel family that is also used elsewhere with callback disabled”
- newest parser-vs-builder comparison:
  - `vm_net_mock_build_actor_info()` currently matches the confirmed `parse_actorinfo_response()` (`0x0100FA88`) fresh-enter read order for the fields relevant to the visible self actor:
    - `actorId=10001`
    - `visualVariant=job-1`
    - `visualGroup=sex+1`
    - role/name string
    - player/display strings
    - HP/MP current/base/display-max scalars
    - target bytes
    - `shortLabel`
    - direct preview image
    - trailing actor `.actor` resource
    - actor-resource scalar
    - trailing scene key
    - final two signed-16 scalars
  - evidence: full decompile of `parse_actorinfo_response()` at `0x0100FA88..0x0100FD22` cross-checked with `src/main.c:vm_net_mock_build_actor_info()`.
  - `vm_net_mock_append_actor_other_empty10_object()` matches `sub_1012958()` (`0x01012958`) as a compact `otherinfo` tuple:
    - `u32 actorId`
    - `u8 visualVariant`
    - `u8 visualGroup`
    - `string labelText`
    - `i16/u8 target pair as consumed by the reader helpers`
    - two short/long label strings
    - final grid `i16/i16`
  - evidence: `net_handle_actor_move_info()` case `10` at `0x01012DD6` calls `sub_1012958()`; `sub_1012958()` then calls `scene_node_find_or_create()` and does not touch the body/world move-entry table.
  - `vm_net_mock_build_group_type1_response()`, `vm_net_mock_build_scene_resource_followup_response()`, and `vm_net_mock_build_scene_interaction_followup_response()` do not currently contain a confirmed field-order mismatch that can explain the bad body/world entry. The latest logs show the bad entry is produced before the later `scene-resource-followup` response is consumed.
  - runtime evidence: latest `bin/logs/net_trace.log` shows `trace_actor_motion_callback_handoff ... tailShorts=3,2,223,382,8,1,5,1`, then `trace_actor_move_entry_append ... actorId=1 box=203,402,240,422`, before the accepted `source=builtin-scene-resource-followup`.
  - conclusion: the current body/world sprite defect should not be fixed by rewriting `actorinfo`, `1/2/10 otherinfo`, or `14/*` response builders unless new parser evidence appears. The confirmed mismatch remains the local pairing of a valid actorinfo scene key with `sub_100F094+1`, causing `.sce` portal tokens to be consumed as a move-entry stream.
- newest coordinate-alignment experiment:
  - status: `confirmed useful`, but final visual correctness still needs operator observation
  - IDA `parse_scene_response_entry()` (`0x010396D6`) reads server field `posinfo` as two tagged `i16` values and forwards them into the active scene object method `+116` together with the scene string.
  - `ui_apply_named_posinfo_target()` (`0x0100E9B8`) consumes the same `posinfo` grammar for the `0x1B/12` target/location family.
  - `tools/inspect_sce.py` parses the active `c00蓬莱仙岛_01.sce` first edge portal as spawn point `(223,382)`, while the older mock still sent `posinfo=(120,120)` and defaulted actorinfo/otherinfo grid fields to `12,10`.
  - `src/main.c` now defaults scene `posinfo`, the `0x1B/12` target `posinfo`, actorinfo trailing grid/motion words, and compact `1/2/10 otherinfo` grid words to `(223,382)`, with `CBE_SCENE_POS_X/Y` and the existing actor grid env vars kept as overrides.
  - runtime evidence from the next rerun confirms the value reaches the active protagonist node: `trace_scene_current_node_draw ... actorId=10001 ... grid=223,382 ... screen=103,65`, replacing the earlier `grid=12,10` / `screen=0,-67`.
  - the portal-derived body/world entry still appears as `actorId=1`, `box=203,402,240,422`, and `screen=-118,-347`; keep it as a separate local scene-resource/callback question rather than the primary self-actor coordinate seed.
- 2026-06-07 follow-up after removing host-side active state advancement:
  - status: `confirmed` for observed trace shape; `hypothesis` for the remaining progress/overlay semantic.
  - current trace does **not** show a new outbound WT request after the `1/12/1 + 1/7/42 + 1/6/1 + 1/6/13 + 1/6/14 + 1/2/10 + 1/25/5` request is answered by `builtin-scene-resource-followup`.
  - however, this does **not** prove the progress strip is merely harmless local residue. The same run keeps `pendingResync=1` in `trace_scene_runtime_tick`, and the scene list handlers show weak/empty list consumption:
    - `30/3` reaches `parse_scene_npcinfo_list()` / `0x01039372`.
    - `30/7` reaches `sub_1039430()` / `0x01039430`.
    - both functions clear the loading-widget gate at `R9+0x5530`, but only populate room/role tables when their raw `roomlist` / `rolesinfo` stream pointer is nonzero and parseable.
  - current source adds `trace_progress_strip_wrapper_tick` so the next manual run can distinguish a one-frame bootstrap strip from a stable-scene strip that continues after `loadFlags=0,0,0`.
  - evidence:
    - latest `bin/logs/net_packets.log` ends the scene sequence with `source=builtin-scene-resource-followup responseLen=319` and no newer unhandled packet.
    - latest `bin/logs/net_trace.log` shows `trace_progress_strip_wrapper ... caller=010044b2 tick=140`, then later `runtime_state ... loadFlags=0,0,0` and repeated `trace_scene_runtime_tick ... pendingResync=1`.
    - IDA: `parse_scene_npcinfo_list()` reads `curpage/pagenum/colnum/colnames/roomnum/roomlist/npcnum/npcinfo`; `sub_1039430()` reads `roomid/colnum/colnames/rolenum/rolesinfo`.
- same run re-confirms the sprite-fragment source:
  - current build no longer had the earlier portal move-entry append guard, so `sub_100F094()` again published the `.sce` portal-shaped candidate into the body/world move-entry table.
  - evidence: `trace_actor_motion_callback_handoff ... tailShorts=3,2,223,382,8,1,5,1`, then `trace_actor_move_entry_append ... actorId=1 grid=223,382 box=203,402,240,422 kind=2`, followed by repeated `trace_scene_body_draw_dispatch ... callbackFn=01004e9d`.
  - source now restores the narrower append-count guard at `0x0100F468`: it skips only this confirmed portal-shaped candidate before `entryCount` is incremented, preserving `sub_100F094()`'s other side effects. This deliberately avoids the earlier whole-callback skip that correlated with losing the top-center map name.
- 2026-06-08 latest manual rerun correction:
  - `confirmed`: the restored narrow `0x0100F468` guard fired on the current session. Runtime logs show `trace_portal_move_entry_append_skipped ... actorId=1 grid=223,382 box=203,402,240,422 kind=2`, then `trace_actor_move_entry_table ... count=0`.
  - `confirmed`: after that skip in the latest session, no newer `trace_actor_move_entry_append` or `trace_scene_body_draw_dispatch` appears; the current protagonist path remains active via `trace_scene_current_node_draw ... actorId=10001 ... grid=223,382 ... screen=103,65`.
  - `corrected`: the repeated post-load `trace_progress_strip_wrapper_tick ... caller=01005a68 dst=0,67` is not the center progress bar. IDA maps `0x01005A0C/0x01005A68` to `DF_PictureLibrary.c` image-library drawing, and the broad overlap filter captured normal map/background draws.
  - `confirmed`: the center/bootstrap strip owner remains `sub_1004362()` / `caller=010044b2`, and in this latest session those hits occur during scene/bootstrap around `tick=126`; the stable scene tail after `loadFlags=0,0,0` does not currently prove that this same center strip is being redrawn every frame.
  - `corrected`: `trace_scene_list_return ... packet=00000000` for the `30/7` handler should not be treated by itself as proof that `rolesinfo` was null or unparseable. Static IDA shows `sub_1039430()` returns `0` normally. The `colnames` stream is confirmed parseable by four `trace_scene_colnames_item` hits; direct list-item parse success still needs a narrower trace if `pendingResync=1` remains suspect.
  - `hypothesis`: `pendingResync=1` still marks an unresolved scene/UI resync question, but the current logs weaken the previous claim that a visible loading strip necessarily means a missing or still-waited server response.
- 2026-06-08 later rerun correction:
  - `confirmed`: after narrowing out the noisy `0x01005A68` picture-library path, the remaining steady strip trace is the real scene `loading.gif` widget. The latest log shows `trace_progress_strip_wrapper_tick ... caller=010460ca ... dst=20,188` once per tick from `tick=401` through the log tail while `sceneTickGate=1,1` and, from `tick=402`, `loadFlags=0,0,0`.
  - `confirmed`: the network tail still ends at the accepted `builtin-scene-resource-followup` response with no newer outbound WT request, so this is local scene/HUD widget scheduling rather than an observed missing packet wait.
  - `confirmed`: the portal/body-entry source remains blocked in this same run (`trace_portal_move_entry_append_skipped` then `trace_actor_move_entry_table ... count=0`), and the protagonist continues through `trace_scene_current_node_draw`.
  - `static correction`: `loading_gif_widget_draw()` at `0x010461A8` always calls `loading_gif_widget_draw_frame()` at `0x010461CA`, even when the global gate at `R9+0x5530` is clear. That gate only toggles widget animation state (`widget+0x10` / frame counter), not whether the widget owner calls draw.
  - `next evidence`: source now adds a read-only entry trace at `0x010461A8` for the scene widget rooted at `R9+0x60F4`, logging the high-level LR/caller plus `widget+0x10`, `R9+0x5530`, frame counter, and image mode. Use the next manual run to identify the owner still dispatching the loading widget.
- 2026-06-08 owner trace result:
  - `confirmed`: the steady scene loading widget is dispatched from `scene_runtime_tick()` through `sub_1013C8A()`. Runtime logs show `trace_loading_gif_widget_draw_entry ... lr=01013cbb caller=01013cba ... widget=01056cc4 args=20,188,200`.
  - `static evidence`: `sub_1013C8A()` (`0x01013C8A`) calls `sub_1013BDC()`, then draws `R9+0x60F4` only when `s16[R9+0x9590] <= 0` and scene gate bytes `R9+0x5C67` / `R9+0x5C68` are both nonzero; IDA shows the indirect widget call at `0x01013CB8`.
  - `confirmed`: later in the same run, the widget state has `flag10=0`, `gate5530=0`, and `frame=0`, but `sub_1013C8A()` still dispatches it. This confirms the remaining visible bar is not caused by the animation gate still being enabled.
  - `confirmed`: `sub_1013C8A()` is called unconditionally by the late draw portion of `scene_runtime_tick()` after `sub_1013D46()` (`0x010157D0..0x010157D4`).
  - `next evidence`: the runtime trace now logs `sceneGate=...` and `overlayCounter=...` directly in `trace_loading_gif_widget_draw_entry` so the next run can prove which of the `sub_1013C8A()` conditions keeps the widget owner active.
- 2026-06-08 newest manual rerun:
  - `confirmed`: `bin/logs/net_packets.log` still ends with the accepted `builtin-scene-resource-followup` (`WT len=49`, response objects `1/12/1,1/7/42,1/6/1,1/6/13,1/6/14,1/2/10,1/25/5`) and no newer unhandled WT request.
  - `confirmed`: the scene loading widget owner condition is exactly true in the latest runtime: `trace_loading_gif_widget_draw_entry ... sceneGate=1,1 overlayCounter=0 ... caller=01013cba`. Static IDA shows those are the `sub_1013C8A()` branch inputs.
  - `static correction`: `R9+0x9590` is better described as the `R9+0x9588` manager's queued-entry count, not a direct overlay flag. IDA shows `sub_10366AC()` appending a manager node and incrementing `*(u16 *)(R9+0x9588+8)` at `0x01036700`; when it is zero, `sub_1013C8A()`'s idle/empty-queue branch may draw the scene `loading.gif` widget.
  - `confirmed`: the earlier portal/body move-entry artifact source remains blocked in this same run: `trace_portal_move_entry_append_skipped ... actorId=1 grid=223,382 box=203,402,240,422 kind=2`, then `trace_actor_move_entry_table ... count=0`, with no later `trace_scene_body_draw_dispatch`.
  - `hypothesis`: the remaining visible actor-adjacent fragments are therefore not yet explained by the old portal/body-entry path. Source now adds a read-only `trace_scene_actor_region_draw` in the image blit shims for the next manual run, limited to settled-scene draws overlapping the actor/loading region.
- a further static split now identifies the alternate name source used by that callback-disabled branch:
  - in `sub_100F094()`, parsed property kind `0x12` stores a string into `R9+0x5C64+0x74` (`0x5CD8`)
  - property kind `0x16` separately stores the centered status text into `R9+0x5C64+0x78` (`0x5CDC`)
  - the later `scene_hud_main_panel_init()` refresh at `0x0100F702` uses `[R9+0x5CD8]` as its `namePtr`, still with `mode=10`, but callback slot `0`
- implication:
  - there is now a concrete non-`sceneKey` string buffer already participating in the same local panel family
  - this strengthens the hypothesis that fresh scene-enter may be using the wrong name slot in addition to, or instead of, the wrong callback pairing
- deeper static parsing narrows that alternate buffer's role:
  - `R9+0x5C64+0x74` (`0x5CD8`) is written only from the `actorTag=5/6/7` branch of `sub_100F094()`
  - that same branch assigns per-node prompt-template kinds `14/15/11`, so it belongs to a prompt / hotspot scene-node family rather than to self-body move records
  - `field kind 0x16` in the same branch separately feeds `R9+0x5C64+0x78` (`0x5CDC`) as the centered scene HUD status text
- consumer-side evidence points the same way:
  - `sub_10183A0()` only selects a candidate node for the later panel/hotspot flow when `R9+0x5CD8` is non-null and the transient prompt-node state is otherwise eligible
  - `sub_100F094()` then reuses that same `R9+0x5CD8` slot in its callback-null `scene_hud_main_panel_init(..., mode=10)` refresh path
- implication:
  - `R9+0x5CD8` now reads more like a transient prompt / hotspot action-label source than like a persistent map descriptor name
  - therefore the current fresh-enter bug is best summarized as “persistent scene-key got routed into a panel family whose alternate normal name source is prompt/hotspot-local”
- write-order / availability follow-up now narrows the first-call timing:
  - the only confirmed write to plain `R9+0x5C64+0x74` (`0x5CD8`) is `sub_100F094()` at `0x0100F69C`
  - nearby stores such as `sub_1013D46()` target `R9+0x5CE4+0x74`, a different scene-UI layout block
  - `scene_runtime_init_and_sync()` fresh-enter path normalizes `sceneKey(R9+0x5E46)` and calls `scene_rebuild_runtime_nodes(..., currentMapIdText=R9+0x5E46)` before that downstream `sub_100F094()` family has had a chance to generate any prompt-local `0x5CD8` text
- implication:
  - `0x5CD8` cannot explain the very first fresh-enter `namePtr` on its own, because it does not exist until after the callback family starts running
  - this shifts the highest-confidence bug description again:
    - not “fresh-enter should just have used `0x5CD8` first”
    - but “fresh-enter is invoking the `mode=10` panel family too early with a non-null `sub_100F094` callback, before the prompt-local name source even exists”
- caller-family comparison now sharpens which path is likely the root producer:
  - `scene_rebuild_runtime_nodes()` is the first direct source that hardcodes `callback=sub_100F094+1`
  - `sub_100F094()` itself later hardcodes the same `mode=10` family with `callback=0`
  - `scene_hud_main_panel_sync_message()` case 6 can replay either callback choice depending on its incoming `n3` flag, but it is not an earliest source; it is only reached via `sub_1037000()` after `MMORPGTempbin` temp-data commit / resource completion
- implication:
  - the freshest static evidence still points to the first fresh-enter `scene_rebuild_runtime_nodes()` invocation as the primary origin of the bad non-null callback pairing
  - later sync-message reinitializations are better treated as downstream replays of already-seeded panel state, not as the original cause
- replay-structure follow-up confirms that downstream model:
  - `scene_hud_main_panel_sync_message()` first reconstructs its inputs from a persisted panel/message record at `R9+38284`
  - persisted fields include:
    - `type`
    - a `0x1E`-byte payload blob
    - `namePtr/currentActorName`
    - `varg_r3`
    - `n19202288`
    - callback-choice byte `n3`
    - two auxiliary handler pointers/contexts at `+60/+64`
  - case 6 then replays `scene_hud_main_panel_init()` by choosing callback `0` vs `sub_100F094+1` solely from that stored `n3` byte
- implication:
  - replay case 6 is a serializer/replayer for an existing `{namePtr, mode=10, callbackChoice}` tuple
  - it does not weaken the current root-cause reading, because it still depends on an earlier producer having already chosen the bad non-null callback variant
- a follow-up file-op pass disproves the earlier `R9+0x4D68` replay-writer hypothesis:
  - `startup_screen_commit_temp_data_file_into_game_data()` uses the same `R9+0x4D68` object with slot `+0x08` as an existence test and `+0x24` as a delete/remove step on a normalized destination path
  - `sub_10370C2()` uses neighboring `R9+0x4D68` slots `+0x10/+0x18/+0x20` inside the `mmorpgTempdata` path, which matches temp-file I/O behavior rather than panel replay serialization
  - `sub_10368CA()` itself only builds a normalized destination path (`var_114`), normalizes a second caller-supplied path/string (`var_214`), and then calls `(*(R9+0x4D68)+0x28)(2, var_114, var_214)`
  - evidence: `0x0103A5C0..0x0103A5D8`, `0x010370F4..0x01037136`, and `0x010368CA..0x010369CA`
- implication:
  - `R9+0x4D68` should now be treated as a file/path manager family
  - slot `+0x28` is best interpreted as a normalized install/move/copy operation, not as the writer of the `scene_hud_main_panel_sync_message()` replay tuple at `R9+38284`
  - the replay tuple still exists and is still consumed by `scene_hud_main_panel_sync_message()`, but its first seed must be chased elsewhere
- the concrete owner of that replay tuple is now clearer:
  - `sub_1037880()` initializes a 72-byte controller at `R9+38280`
  - this controller exposes:
    - `+0x38 = sub_10365F0` (general record writer)
    - `+0x3C = sub_10366AC` (narrow subtype-3 writer)
    - `+0x40 = send_update_chunk_request`
    - `+0x44 = handle_version_update_response`
  - evidence: `0x01037880..0x010378DE`
- `sub_10365F0()` now gives the first concrete write layout for the record later replayed from `R9+38284`:
  - `+0x00`: record type
  - `+0x02..+0x1F`: copied payload blob
  - `+0x24`: extra resource/value field for types `1/4/5/6`
  - `+0x28/+0x2A/+0x2C/+0x2E`: four halfword parameters
  - `+0x30`: callback-choice / mode byte
  - `+0x3C/+0x40`: pointer/context pair later re-read by `scene_hud_main_panel_sync_message()`
  - evidence: `0x010365F0..0x010366A8`
- `sub_10366AC()` is a narrower helper that:
  - sets the record type
  - zeroes the payload
  - copies a short string into `+0x02`
  - forces byte `+0x30 = 3`
  - publishes the record as the current `R9+38284` entry
  - evidence: `0x010366AC..0x01036708`
- implication:
  - the replay tuple consumed by `scene_hud_main_panel_sync_message()` is now confirmed to be seeded by the `R9+0x9588` controller's writer methods, not by the `R9+0x4D68` file/path manager
- the direct caller families are now partially resolved too:
  - `sub_100D3EE()` queues type `1`
  - `sub_100DB82()` queues type `4`
  - `sub_100D564()` queues type `5`
  - `parse_actor_motion_descriptor()` queues type `6`
  - all four use `get_net_manager_object()+0x38`, i.e. `sub_10365F0`
  - evidence: `0x0100D452..0x0100D47E`, `0x0100DE86..0x0100DEB0`, `0x0100D6BC..0x0100D6DE`, and `0x0100DB4A..0x0100DB74`
- the fresh-enter / scene-HUD relevant branch is now the type-6 one:
  - the fallback tail of `parse_actor_motion_descriptor()` directly emits
    - `sub_10365F0(6, v46, a5, a1, *(R9+23240), v44, v45, a8 != 0)`
  - this means the case-6 replay tuple later consumed by `scene_hud_main_panel_sync_message()` is seeded by the descriptor/parser fallback itself, with callback-choice byte `+0x30 = (a8 != 0)`
  - evidence: full decompile of `0x0100D6E2`, especially the `else` tail at `0x0100DB4A..0x0100DB74`
- corrected field semantics for that type-6 tuple:
  - record `+0x24` is the target HUD panel/controller object pointer, not the `namePtr`
  - the replayed `namePtr` comes from the payload blob at `+0x02..+0x1F`
  - case-6 replay reconstructs the original `scene_hud_main_panel_init()` call as:
    - `R0 = record+0x24`
    - `R1/R2 = two packed ints rebuilt from halfwords at +0x28..+0x2F`
    - `R3 = record+0x3C`
    - stacked extra0 = copied payload blob (`namePtr`)
    - stacked extra1 = constant `10`
    - stacked extra2 = callback `0` vs `sub_100F094+1`, chosen from byte `+0x30`
  - evidence: `0x0103678E..0x010368C4` cross-checked with the original producer callsite at `0x0100F8E0..0x0100F8F6`
- current best type-6 producer mapping is therefore:
  - payload `+0x02 = a5` (descriptor/name string)
  - panel object `+0x24 = a1`
  - halfword quartet `+0x28..+0x2F = { low/high16(a2), low/high16(a3) }`
  - callback-choice byte `+0x30 = (a8 != 0)`
  - `R3` replay arg `+0x3C = a4`
  - aux/context `+0x40 = *(R9+0x5AC8)`
- corrected meaning of the `+0x40` field:
  - `*(R9+0x5AC8)` is the parser-method-table pointer stored by `sub_100DEB4()` at `R9+0x5AC4 + 4`, not a generic opaque scene context
  - `sub_100DEB4()` also stores `a2/a3` beside it at `R9+0x5ADC` / `R9+0x5AE0`
  - evidence: full decompile/disassembly of `0x0100DEB4`
- narrowed source of the halfword quartet:
  - the fresh-enter path in `scene_runtime_init_and_sync()` reads two signed halfwords from the local actor/UI controller reached through `R9+0x5C64+0x44`, then passes them into `scene_rebuild_runtime_nodes()`
  - `scene_rebuild_runtime_nodes()` preserves those values when it primes `{ currentMapIdText, 10, sub_100F094+1 }` for `scene_hud_main_panel_init()`
  - therefore type-6 record `+0x28..+0x2F` is best read as a locally derived controller/viewport pair, not as scene-key text or a network field copied out of `1/2/10 otherinfo`
- narrowed replay-side consumer scope:
  - `scene_hud_main_panel_sync_message()` case 6 does not consume record `+0x40`
  - the case-6 replay path uses only:
    - `+0x24` -> HUD panel object (`R0`)
    - `+0x28..+0x2F` -> rebuilt local ints (`R1/R2`)
    - `+0x3C` -> replay `R3`
    - `+0x30` -> callback-choice byte
    - payload `+0x02..+0x1F` -> stacked `namePtr`
  - evidence: `0x010368A2..0x010368C4`
- corroborating local-owner evidence:
  - `sub_1010228()` builds the `R9+0x5CE4` scratch summary block by copying a string from `+0x44` and multiple scalar fields from the same nearby `R9+0x5C64+0x40/0x44` pointer family (`+0x24`, `+0x64`, `+0x80+0x34/0x38`, `+0xC0+0x04/0x08`, status bytes near `+0x140`)
  - evidence: `0x01010228..0x0101029C`
  - implication: the current type-6 replay tuple is now best understood as being sourced from a local scene-status snapshot/controller object family
- resolved meaning of record `+0x3C` on the original fresh-enter path:
  - `scene_rebuild_runtime_nodes()` loads `R4 = R9 + 0x5F08` and forwards it as:
    - `R3` to `scene_hud_main_panel_init()`
    - `R1` to `scene_hud_prompt_grid_init()`
  - therefore type-6 record `+0x3C` is best read as a pointer into the shared scene HUD actor-asset/controller root at `R9+0x5F08`
  - evidence: `0x0100F89E..0x0100F90A`
- narrowed shape of the packed local ints:
  - `scene_runtime_init_and_sync()` seeds two packed `s16` pairs before fresh-enter rebuild:
    - `{0, 67}`
    - `{240, 293}`
  - those same packed ints flow into both `scene_hud_main_panel_init()` and `scene_hud_prompt_grid_init()`
  - `scene_hud_prompt_grid_init()` stores the first pair into local shorts `+0xA/+0xC` and mirrors their negatives into `+0x2/+0x4`, strongly suggesting a viewport/window origin+extent contract
  - evidence: `0x0101301C..0x0101302C`, `0x0100F8EC..0x0100F90A`, `0x01046CB2..0x01046CD6`
- traced one layer further upstream, those packed ints originate from the actorinfo blob:
  - `parse_actorinfo_response()` fresh-enter path writes the last two `v34()` dwords from the stream into `sceneStatusSnapshotNode + 240/+244`
  - `scene_runtime_init_and_sync()` later forwards the low-16-bit view of those same two stored dwords into `scene_rebuild_runtime_nodes()`
  - evidence: `0x0100FD10..0x0100FD22` and `0x010134D2..0x01013586`
  - implication: the current fresh-enter tuple `{0,67}` / `{240,293}` is best treated as the low-halfword projection of the actorinfo blob's final two trailing `i32` fields, not as a separately invented local constant
- corrected by a later static pass:
  - those two final fresh-enter scalars are read by `v34()`, the same helper used for `actorResourceArg`
  - the stronger current reading is therefore “signed-16 values widened into dword slots at `sceneStatusSnapshotNode + 240/+244`”, not literal trailing actorinfo `i32`
  - evidence: `parse_actorinfo_response()` at `0x0100FCEA..0x0100FD22`
- corrected consumer mapping for the same pair:
  - `scene_runtime_init_and_sync()` still sign-extends `sceneStatusSnapshotNode + 240/+244` and passes them in `R1/R2` to `scene_rebuild_runtime_nodes()`
  - but `scene_rebuild_runtime_nodes()` does **not** feed those values into `scene_hud_main_panel_init()` or `scene_hud_prompt_grid_init()`
  - the only visible in-function use is carrying them as extra `R1/R2` on `scene_node_reset_at()` calls inside the node-clone loop, and `scene_node_reset_at()` itself does not consume those registers
  - evidence:
    - `scene_runtime_init_and_sync()` at `0x010134D2..0x01013586`
    - `scene_rebuild_runtime_nodes()` at `0x0100F832..0x0100F90A`
    - `scene_node_reset_at()` at `0x010459EC..0x01045A94`
- implication:
  - for the current `c00蓬莱仙岛_01 -> sub_100F094` scene-entry bug, the actorinfo tail pair at `+240/+244` is no longer a first-order suspect for the bad mode-10 panel family
  - the strongest remaining first-order pair is still `payload namePtr = currentMapIdText/sceneKey` plus callback-choice `(a8 != 0)`
- corrected ownership note for the generic-init path:
  - the heap-local function tables written by `scene_object_vtable_init(*[R9+0x54AC])` at offsets like `a1 + 1032 .. a1 + 1088` are not the same thing as the fixed global ops descriptor returned by `scene_get_actor_asset_ops_descriptor()`
  - `scene_hud_main_panel_init()` still dereferences `R9+0x5C48 + 0x10`, while `scene_object_vtable_init()` operates on the separately allocated scene object pointer stored in `[R9+0x54AC]`
  - evidence:
    - `scene_system_bootstrap()` allocation/call chain at `0x010038BC..0x010038CC`
    - `scene_get_actor_asset_ops_descriptor()` at `0x01018058..0x0101805C`
- implication:
  - do not equate heap-local scene-object slots such as `a1+1040/a1+1044` with the fixed global `R9+0x5C58/+0x5C5C` callbacks consumed by `scene_hud_main_panel_init()` and `scene_runtime_init_and_sync()`
  - the true writer for the fixed global `R9+0x5C48` descriptor remains unresolved
- narrowed further by static scan and loader semantics:
  - a focused whole-binary `0x5C48` scan found only the known reader family and literal-pool copies:
    - `scene_runtime_init_and_sync()`
    - `scene_get_actor_asset_ops_descriptor()`
    - `scene_hud_main_panel_init()`
    - `scene_actor_asset_slot_table_load_entry()`
    - `scene_draw_node_overhead_overlay()`
    - `sub_10183A0()` literal-pool reference
  - no additional main-binary store site for the base descriptor was exposed
  - constructor review of nearby scene/bootstrap initializers also only found writers for adjacent but different objects:
    - `sub_10035E6()` on `R9+0x55AC`
    - `sub_1048FEE()` on `R9+0x5EF0`
    - `sub_1031790()` on the large `R9+0x7CB4` callback bank
    - `scene_actor_asset_slot_table_init()` on `R9+0x5FD0`
- stronger current interpretation of the fixed ops block:
  - the emulator loader copies the module's data bytes from `fileBuffer + g_cbeInfo.BssDataOffset` into `ROM_ADDRESS + g_cbeInfo.headerInt2`, then sets `Global_R9 = ROM_ADDRESS + g_cbeInfo.headerInt2`
  - evidence: `src/main.c:9141-9146`, `src/main.c:9158`, `src/cbeParser.c:80-124`
  - implication:
    - `R9+0x5C48` lives in the copied module data image, not in the heap-local scene object built at `[R9+0x54AC]`
    - the shared actor-asset ops descriptor is now more likely image-seeded / loader-relocated small-data than a scene-local runtime constructor product
    - for the active fresh-enter `sceneKey + sub_100F094` misroute, the next high-value static target is to identify the preseeded callback values already present at `R9+0x5C48 + {8,0xC,0x10,0x14}`, not to keep searching for a missing scene-bootstrap writer in the main binary

## 2026-06-08 latest operator rerun: actor-adjacent fragments are progress-panel tiles

- confirmed fragment draw owner:
  - the new read-only actor-region blit trace hit its cap with 160 samples.
  - every sample in the visible actor/loading region uses `vMDrawImageWithClipEx()` from `srcInfo=05016cd0`, `srcPtr=05019d98`, `srcDim=36,36`, with source rectangle `sx=12 sy=12 w=12 h=12`.
  - the destination tiles advance by 12-pixel steps across the center region, e.g. `dx=48,60,72...180` and `dy=192,204...348`.
  - the immediate return path is `lr=01004141`, `caller=01004175`, `last=0100413e`.
  - static IDA maps that path to `sub_1004096()` / `sub_1004150()`, and the higher caller `0x010044B2` is inside `sub_1004362()`, the 36x36 nine-slice/panel tiler.
- corrected interpretation:
  - the currently visible "fragments" are not confirmed as actor body-frame fragments.
  - they are confirmed as the same tiled panel/progress skin path already logged by `trace_progress_strip_wrapper ... caller=010044b2`.
  - the older portal-derived `ActorMoveEntryEx` source remains blocked in this run (`trace_portal_move_entry_append_skipped`, then `trace_actor_move_entry_table ... count=0`), and no `trace_scene_body_draw_dispatch` appears after that.
- loading owner remains active:
  - runtime still shows repeated `trace_loading_gif_widget_draw_entry ... caller=01013cba ... sceneGate=1,1 overlayCounter=0 ... args=20,188,200`.
  - static IDA confirms `sub_1013C8A()` dispatches the widget at `0x01013CB8` when `s16[R9+0x9590] <= 0` and `R9+0x5C67/0x5C68` are both nonzero.
  - therefore the visible panel/strip and the actor-adjacent fragments are now best treated as one symptom: the scene loading/progress widget owner remains scheduled after the scene has otherwise reached stable `status_panels` / `actor_pass`.
- map-name/status correction:
  - `sub_100F094()` only writes the top-center scene/status text in the `actorTag == 4` branch, property kind `22`, storing the read string into `R9+0x5CD8+4` at `0x0100F68A`.
  - the logged portal-tail collision starts at raw `.sce` portal words (`tailShorts=3,2,223,382,8,1,5,1`) and the only skipped candidate is `actorTag == 2`, which only populates the 32-byte move-entry table.
  - hypothesis: the missing map title in the latest visual run means the current scene-key/descriptor stream did not deliver a valid `actorTag=4/property=22` status record, or that path was never reached after the `.sce` portal grammar collision. This is not yet evidence for an `actorinfo` field-order mismatch.
- network status:
  - `net_packets.log` still ends after the accepted `builtin-scene-resource-followup` response (`WT len=49`, response objects `1/12/1,1/7/42,1/6/1,1/6/13,1/6/14,1/2/10,1/25/5`).
  - no newer WT request or unhandled packet appears in this run.

### 2026-06-08 follow-up correction: status record exists, dispatch completion is still open

- corrected by the next manual run:
  - the extended callback-tail trace shows more than the portal prefix:
    - `tailShorts=3,2,223,382,8,1,5,1,3,6,...`
    - `tailHex` continues into a second scene descriptor filename (`c00蓬莱仙岛_02.sce`) and additional record tokens.
  - `sub_100F094()` reaches both scene text write sites:
    - `0x0100F6A0`: property kind `0x12` writes `R9+0x5CD8` (`trace_scene_status_text_write ... newText=<empty>`)
    - `0x0100F68A`: property kind `0x16` writes `R9+0x5CDC` (`trace_scene_status_text_write ... newText=蓬莱-铜雀台`)
  - the final `0x0100F78E` snapshot reports `count=0` and `statusText=蓬莱-铜雀台`.
- implication:
  - the older "no actorTag=4/property=22 status record" hypothesis is disproven for this run.
  - the portal-prefix collision is still real for the move-entry table, but it does not prevent the later status-text record from being parsed.
- remaining loading-widget evidence:
  - `sub_1013C8A()` continues to dispatch the loading widget with `sceneGate=1,1` and manager queue count `R9+0x9590 == 0`.
  - `trace_progress_strip_wrapper_tick` stays on the real `loading.gif` path (`caller=0x010460CA`, dst `20,188`).
- new dispatch hypothesis:
  - `builtin-scene-resource-followup` is queued and fired (`mock_response ptr=050179a0 len=319`, then `fire_event ... r0=050179a0 tick=138`), but current handler traces do not show its `12/1`, `7/42`, `6/*`, `2/10`, or `25/5` objects entering the expected business handlers.
  - next trace target is `net_business_response_dispatch()` itself: entry gate, unpack result/object count, per-object kind/subtype, and early-exit reason.

### 2026-06-08 dispatch-gate confirmation

- confirmed:
  - the scene-resource follow-up packet does enter `net_business_response_dispatch()` with the expected packet pointer and object count:
    - `trace_business_dispatch_state label=entry ... r0=050179a0 ... objectCount=7`
  - it is not unpack failure and not a missing queued event. It is rejected by the dispatch gate check at `0x01012E6E..0x01012E72`:
    - `dispatchGate=0`
    - `trace_business_dispatch_state label=early_gate_off`
  - therefore none of the follow-up response objects (`12/1`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10`, `25/5`) are consumed on this run.
- preceding state:
  - the earlier group/type scene-enter response enters the same dispatcher with `dispatchGate=1`, unpacks 5 objects, and dispatches `5/10`, `10/26`, `30/1`, `30/3`, and `30/7`.
  - after that dispatch's `fallback_exit`, the same gate is already `0`.
- implication:
  - persistent loading is now tied to the fact that scene-resource follow-up data is delivered after the client has closed the scene object's business-dispatch gate.
  - the next question is packet sequencing / bundling / gate ownership: why is `sceneObj+0x164` cleared before the follow-up response that carries the requested resource/misc objects?
  - do not patch the gate open as a fix; trace the writer and then decide whether the mock should include required objects in the earlier dispatch window or trigger the client's expected gate reopening path.

### 2026-06-08 dispatch-gate writer and bundled follow-up experiment

- confirmed writer:
  - the scene gate reopens for scene entry at `pc=0508338c` (`trace_scene_dispatch_gate_write ... old=0 new=1 ... activeScreen=01053450`).
  - during the first scene-enter dispatch, item `30/1` clears the same byte: `trace_scene_dispatch_gate_write ... pc=01039766 ... old=1 new=0`.
  - static IDA identifies the clearing code as `parse_scene_response_entry()` / the `30/1` scene handler. It reads `scene` and `posinfo`, invokes scene-object methods, then stores zero to `sceneObj+0x164` at `0x01039766`.
- confirmed dispatcher behavior:
  - `net_business_response_dispatch()` checks `sceneObj+0x164` once near entry (`0x01012E6E..0x01012E72`) before unpacking/dispatching the packet.
  - in the latest run, the first group/type response dispatches 5 objects: `5/10`, `10/26`, `30/1`, `30/3`, `30/7`.
  - after `30/1` clears the gate, the same in-progress dispatch still continues to `30/3` and `30/7`.
  - later packets enter with `dispatchGate=0`; `7/7 type=2`, `7/7 type=3`, and the `builtin-scene-resource-followup` packet all take `early_gate_off`, so the follow-up's `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10`, and `25/5` objects are not consumed.
- protocol implication:
  - the follow-up packet bytes are not currently the immediate mismatch; the ordering is. The client has already closed the scene business-dispatch gate by the time the mock sends those objects as a separate response.
  - this supersedes earlier wording that treated the accepted `builtin-scene-resource-followup` as proof that the remaining progress/loading panel was purely local residue.
- implementation experiment:
  - `vm_net_mock_build_group_type1_response()` now appends the same seven scene-resource follow-up objects after `30/1`, `30/3`, and `30/7`, keeping them inside the initial dispatch call without forcing `sceneObj+0x164` open.
  - the standalone `vm_net_mock_build_scene_resource_followup_response()` remains and reuses the same helper, so the response body stays byte-shape-aligned while the current sequencing hypothesis is tested.
  - hypothesis: because the dispatcher does not recheck the gate between items, the bundled objects should dispatch in the same call even after `30/1` clears the gate. The next run should confirm this through `trace_business_dispatch_item` before claiming the loading/progress owner is fixed.

### 2026-06-08 oversized bundle negative result

- confirmed negative result:
  - bundling all seven follow-up objects into `group-type1` produced `mock_group_type1_response ... objects=12 len=781`, but the first dispatcher call failed inside packet unpacking:
    - `trace_business_dispatch_state label=entry ... r1=0000030d ... dispatchGate=1`
    - `trace_business_dispatch_state label=unpack_error ... r0=00000003 ... objectCount=10`
  - no per-item dispatch occurred for that first group response, so `30/1` did not close the gate.
  - the later standalone 7-object follow-up was then accepted by the still-open gate and dispatched through `12/1`, `7/42`, `6/1`, and `6/13`, but stopped with a fault before item 4:
    - stdout: `pc=0000000a`, `lr=01012e9f`, `lastPc=01012e9c`, `r1=05001560`, `r5=4`
    - interpretation: after `6/13`, the per-item pre-dispatch callback loaded at `0x01012E9C` was `0xA`, so the next item dispatch tried to branch to an invalid low address.
- protocol implications:
  - a 12-object response is not a valid bundled shape for this scene-enter dispatch window; current evidence suggests the unpack/table path is limited to about 10 objects here.
  - `6/13 tasktypes` and `6/14 task action` must not be treated as confirmed-safe scene-entry follow-up objects in this dispatcher state. Their field shape may be individually parseable, but this runtime path corrupts the next per-item dispatch callback.
- superseded narrowed experiment:
  - `vm_net_mock_build_group_type1_response()` now bundles only five core follow-up objects: `12/1`, `7/42`, `6/1`, `2/10`, and `25/5`.
  - this keeps the group response at 10 objects total: `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, plus the five core follow-up objects.
  - later status: the following run confirmed the 10-object size fits, but same-window `2/10` corrupts the current actor node draw callback. The active experiment is now the 9-object set recorded below.

### 2026-06-08 10-object bundle result and 2/10 downgrade

- confirmed positive result:
  - a 10-object `group-type1` response is accepted by `event_packet_init()`:
    - `mock_group_type1_response ... objects=10 ... len=675`
    - `trace_business_dispatch_state label=after_unpack_ok ... objectCount=10`
  - the dispatcher processes all ten entries in order:
    - `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `12/1`, `7/42`, `6/1`, `2/10`, `25/5`.
  - `30/1` still clears `sceneObj+0x164` at `0x01039766`, and the dispatcher does not recheck the gate between entries. Later separate `7/7 type=2/3` responses are then blocked as expected.
- confirmed negative result:
  - same-window `2/10` corrupts the active actor node draw path on the next frame:
    - stdout reports `pc=9883a110`, `lr=01014597`, `lastPc=01014594`.
    - IDA maps `0x01014594` to `scene_draw_actor_pass()` calling `ActorSceneNode.drawAt` from node offset `+0x148`.
  - before this dispatch, the node publish trace had valid callbacks:
    - `trace_scene_current_node_publish ... current=05400000 ... drawAt=01045579 step=01045429`.
  - therefore `1/2/10 otherinfo` remains matched enough to create/update actor state in some paths, but it is not confirmed safe in the current scene-enter dispatch window. Treat its field completeness/timing as unresolved.
- current narrowed experiment:
  - `vm_net_mock_build_group_type1_response()` now bundles only `12/1`, `7/42`, `6/1`, and `25/5`, for 9 objects total with the base scene-enter response.
  - `2/10` remains only in the standalone follow-up response for comparison; on the normal path that standalone response should be blocked after `30/1` closes the dispatch gate.
  - hypothesis: if the next run avoids the bad `drawAt` crash but the loading panel persists, the remaining blocker is more likely the omitted `6/13/6/14` and/or the scene local state they would normally drive, not `2/10` itself.

### 2026-06-08 9-object bundle result

- confirmed:
  - the current `group-type1` response is accepted with nine objects: `mock_group_type1_response ... objects=9 ... len=579` and `trace_business_dispatch_state label=after_unpack_ok ... objectCount=9`.
  - dispatched order is `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `12/1`, `7/42`, `6/1`, `25/5`.
  - after `30/1` clears `sceneObj+0x164` at `0x01039766`, same-call dispatch still reaches the bundled `12/1`, `7/42`, `6/1`, and `25/5`.
  - the later standalone seven-object scene-resource follow-up is still blocked by `dispatchGate=0`, so its `6/13`, `6/14`, and `2/10` entries are not consumed on the normal path.
- confirmed:
  - removing same-window `2/10` avoids the previous actor-node draw callback fault. Runtime draw evidence stays stable at `scene_draw_actor_pass()` / `0x01014594`: `actorId=10001`, `grid=223,382`, `callback=01045579`, `drawAt=01045579`, `step=01045429`.
  - status text parsing is still present in the descriptor path: `trace_scene_status_text_write ... statusText=蓬莱-铜雀台`, followed by `trace_actor_move_entry_table ... count=0 ... statusText=蓬莱-铜雀台`.
- unresolved:
  - the loading/progress widget remains active through `sub_1013C8A()` with `sceneGate=1,1` and `R9+0x9590 == 0`.
  - the 9-object set is therefore a stable partial match, not a complete scene-entry protocol match.
- next evidence:
  - source now adds read-only write traces for `R9+0x5C67`, `R9+0x5C68`, and `R9+0x9590`. Use the next run to identify which parser or scene runtime path sets these owner inputs and whether a valid response should clear them.

### 2026-06-08 loading-owner gate correction

- confirmed:
  - live writes show `R9+0x5C67` and `R9+0x5C68` are set by `scene_runtime_init_and_sync()` at `0x010132C8/0x010132CA`, before the first group/type1 dispatch.
  - the latest run has no corresponding clear write and no write to `R9+0x9590`; the loading widget remains because `sub_1013C8A()` sees `sceneGate=1,1` and queue count `0`.
- static protocol evidence:
  - `net_handle_business_followup_events()` handles top-level `kind=27/subtype=11` and writes `R9+0x5C67` from the scene object's `fb` state.
  - `kind=27/subtype=12` is the confirmed primary path that writes that `fb` state via `ui_apply_named_posinfo_target_with_fb()`.
- active experiment:
  - the bundled first-window follow-up no longer includes unconfirmed `25/5`.
  - it now includes `27/12(result=1, fb=1, name=sceneKey, posinfo=spawn)` followed by empty `27/11`, alongside `12/1`, `7/42`, and `6/1`.
  - object count remains 10 total in `group-type1`, matching the previously accepted maximum-sized shape while avoiding same-window `2/10` and `6/13` / `6/14`.

### 2026-06-08 27/12 plus 27/11 result

- confirmed:
  - the 10-object first-window bundle is accepted with `len=634` and object order:
    - `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `12/1`, `7/42`, `6/1`, `27/12`, `27/11`.
  - `27/12` and `27/11` both dispatch through `net_handle_fb_target_dispatch()` / `0x01010BC0`.
  - after normal dispatch, `net_handle_business_followup_events()` reaches `case27_11` at `0x010106D8` and clears `R9+0x5C67` at `0x010106F4`.
  - runtime evidence: `trace_scene_loading_owner_write field=sceneGateA_R9_5C67 ... old=1 new=0 ... pc=010106f4`.
- confirmed correction:
  - the former steady scene `loading.gif` owner is no longer active in this run: `trace_loading_gif_widget_draw_entry` count is `0`, and the narrowed progress wrapper has no `trace_progress_strip_wrapper_tick` hits.
  - therefore the current visible actor-adjacent fragments, if still present, are not confirmed to be the old `sub_1013C8A()` / `R9+0x60F4` loading widget.
- traffic consequence:
  - the previous larger `WT len=49` scene-resource-followup request does not occur after this bundle.
  - the client later sends only the small `WT len=14` pair (`12/1`, `7/42`), answered by the existing tail skill response; this later packet is still blocked by `dispatchGate=0` if delivered through the scene business dispatcher.
- hypothesis / next evidence:
  - tail traces now show repeated `sub_1003568 -> sub_100337C(0)` calls with `sceneTickGate=0,1`.
  - source now records actor-region image draws specifically in the `sceneGate=0,1` window, because the earlier trace required `1,1` and exhausted its cap before `27/11` cleared gateA.
  - use the next manual run to decide whether residual fragments are a live redraw from this gateA-cleared overlay path or stale pixels not repainted by the normal scene pass.

### 2026-06-08 27/11 alone leaves the scene tick overlay active

- confirmed from the next manual run:
  - the accepted first-window packet still has `objects=10 len=634`, and dispatch reaches only `27/12` then `27/11` after the core scene entries.
  - there is no `case27_4` trace in this run, so this log predates the new `27/4` builder experiment.
  - `case27_11` clears `R9+0x5C67` at `0x010106F4`, producing `sceneTickGate=0,1`.
  - the old `sub_1013C8A()` / `loading.gif` owner stays stopped: there are no `trace_loading_gif_widget_draw_entry` or narrowed `trace_progress_strip_wrapper_tick` hits.
- confirmed fragment owner:
  - immediately after `R9+0x5C67` is cleared, `scene_runtime_tick()` repeatedly calls `sub_1003568 -> sub_100337C(0)` from the branch at `0x01014D74..0x01014D80`.
  - the actor-region blit trace confirms the visible actor-adjacent fragments are live panel-tile draws in this gate state, not stale pixels:
    - `trace_loading_overlay_call label=sub_1003568 ... sceneTickGate=0,1`
    - `trace_scene_actor_region_draw ... srcDim=36,36 ... sx=12 sy=12 w=12 h=12 ... sceneGate=0,1 loadFlags=1,0,0`
- static evidence for the next packet candidate:
  - `net_handle_business_followup_events()` case `27/4` reads `min`, `result`, `type`, `fb`, and `info`.
  - when `result==1 && type==1`, it calls `sub_1010486()`, stores `fb` into the scene object, optionally copies `info`, then writes `R9+0x5C67=1` at `0x0101087E` and `R9+0x5C68=1` at `0x01010882`.
- implementation hypothesis:
  - the mock should not stop at `27/11`; it likely needs the matching `27/4` ready/finalize object inside the same first scene-enter dispatch window.
  - `src/main.c` now keeps the accepted 10-object limit by replacing the bundled all-empty `6/1 taskinfo` slot with `27/4(result=1,type=1,fb=1,info="")`, yielding follow-up objects `12/1`, `7/42`, `27/12`, `27/11`, and `27/4`.
  - the next manual run should confirm `trace_followup_case label=case27_4` and owner writes restoring both scene tick gate bytes to `1,1`; only after that can the remaining panel fragments be attributed to another wait flag such as `loadFlags=1,0,0`.

### 2026-06-08 27/4 ready branch result

- confirmed:
  - the first-window response with bundled `27/4` is accepted: `mock_group_type1_response ... objects=10 ... len=669`, followed by `trace_business_dispatch_state label=after_unpack_ok ... objectCount=10`.
  - object order is `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `12/1`, `7/42`, `27/12`, `27/11`, `27/4`.
  - `net_handle_business_followup_events()` reaches both `case27_11` and `case27_4`:
    - `case27_11` clears `R9+0x5C67` at `0x010106F4`.
    - `case27_4` then writes `R9+0x5C67=1` at `0x0101087E` and `R9+0x5C68=1` at `0x01010882`.
- confirmed correction:
  - `27/4` fixes the `sceneTickGate=0,1` branch caused by `27/11` alone; the post-`27/11` `sub_1003568` actor-region panel-tile trace no longer continues as the primary tail symptom.
  - however, restoring the gate pair to `1,1` makes the older `sub_1013C8A()` branch true again while `s16[R9+0x9590] == 0`, so the scene `loading.gif` widget is again drawn every tick:
    - `trace_loading_gif_widget_draw_entry ... caller=01013cba ... sceneGate=1,1 overlayCounter=0`
    - `trace_progress_strip_wrapper_tick ... caller=010460ca ... dst=20,188`
- confirmed traffic regression:
  - because the bundled 10-object experiment spent the former `6/1 taskinfo` slot on `27/4`, the client again sends the larger `WT len=49` scene-resource follow-up request.
  - that standalone follow-up response is still rejected by `net_business_response_dispatch()` with `dispatchGate=0`, so its `6/1`, `6/13`, `6/14`, `2/10`, and `25/5` entries are not consumed.
- implementation hypothesis:
  - `6/1 taskinfo` must remain in the first scene-enter dispatch window, and the `27/12/27/11/27/4` trio must also remain there; the lower-priority `12/1 + 7/42` skill/book pair can be deferred because the client already has a separate small request path for it.
  - `src/main.c` now changes the bundled first-window follow-up set to `6/1`, `27/12`, `27/11`, and `27/4` for 9 total objects with the base scene entries.
  - next verification should check whether the large `WT len=49` request disappears again, whether only the small `12/1 + 7/42` request remains, and whether `R9+0x9590` / `sub_1013C8A()` changes after `loadFlags=0,0,0`.

### 2026-06-08 9-object taskinfo plus fb-target trio result

- confirmed:
  - the 9-object first-window response is accepted with `mock_group_type1_response ... objects=9 ... len=613`.
  - object order is `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `6/1`, `27/12`, `27/11`, `27/4`.
  - `case27_11` and `case27_4` still execute, and `27/4` restores `R9+0x5C67/5C68` to `1,1`.
- confirmed negative result:
  - the large `WT len=49` scene-resource follow-up request still returns.
  - because the only intentionally omitted objects are now `12/1 + 7/42`, this proves the skill/book pair also belongs in the first scene-enter dispatch window if the goal is to avoid the large blocked follow-up.
  - the standalone large response is still rejected by `dispatchGate=0`.
- current hypothesis:
  - the first-window set needs `12/1`, `7/42`, `6/1`, and the ready/finalize `27/4`.
  - the `27/12 + 27/11` pair is now lower priority for the next experiment: `27/11` is confirmed to create the bad `sceneTickGate=0,1` branch when not immediately finalized, and including both consumes two scarce object slots.
  - `src/main.c` now uses first-window follow-up objects `12/1`, `7/42`, `6/1`, and `27/4`, for 9 total objects.
- next verification:
  - check whether the large `WT len=49` disappears again.
  - check whether `case27_4` alone reaches the ready/finalize side effects without `27/12/27/11`.
  - if the large request disappears but `loading.gif` persists with `overlayCounter=0`, move the next static/runtime target to the `R9+0x9588` manager-count producer path.

### 2026-06-08 12/1 + 7/42 + 6/1 + 27/4 negative result

- confirmed from the latest operator rerun:
  - the current 9-object first-window packet is accepted: `mock_group_type1_response ... objects=9 ... len=615`.
  - object order is `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `12/1`, `7/42`, `6/1`, `27/4`.
  - `net_handle_business_followup_events()` reaches `case12_1` and `case27_4`; `27/4` writes `R9+0x5C67/0x5C68` to `1,1`.
  - the client still sends the large `WT len=49` request for `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10`, and `25/5`.
  - the standalone large response again enters `net_business_response_dispatch()` with `dispatchGate=0` and is not consumed.
  - the visible strip/fragments remain the `sub_1013C8A()` / `loading.gif` owner path: `trace_loading_gif_widget_draw_entry ... caller=01013cba ... sceneGate=1,1 overlayCounter=0`.
- implication:
  - matching `12/1`, `7/42`, `6/1`, and `27/4` is not sufficient to satisfy the client's first scene resource wait.
  - the next likely missing first-window entries are among `6/13`, `6/14`, and `25/5`; same-window `2/10` remains excluded because an earlier accepted 10-object run corrupted `ActorSceneNode.drawAt` at `scene_draw_actor_pass()` / `0x01014594`.
- active experiment:
  - superseded by the next result below.

### 2026-06-08 6/13 same-window negative result

- confirmed from the latest operator rerun:
  - the first-window packet with `6/13` is accepted at the WT packet layer: `mock_group_type1_response ... objects=10 ... len=449`, followed by `trace_business_dispatch_state label=after_unpack_ok ... objectCount=10`.
  - object order starts as intended: `5/10`, `10/26`, `30/1`, `12/1`, `7/42`, `6/1`, `6/13`.
  - immediately after dispatch item index 6 (`kind=6 subtype=13`), the next per-item callback is corrupted:
    - stdout: `pc=04710400`, `lr=01012e9f`, `lastPc=01012e9c`, `r5=7`.
    - runtime trace: `trace_scene_loading_owner_write ... pc=0104d43c/0104d410`, where IDA identifies those addresses as memcpy internals, not legitimate scene-gate writers.
  - static IDA at `net_handle_task_response_dispatch()` case `13` (`0x01047896`) shows it initializes a stream from field `tasktypes`, then loops `R4=2..7`: read a short, write the type id into `R9+0x580C + 0x16*i + 0x14C`, clear a 10-byte name slot, read a length/pointer pair, and memcpy that many bytes into the name slot.
- corrected interpretation:
  - the current `vm_net_mock_append_tasktypes_empty13_object()` encoding is a mismatch for the case-13 inner string/length reader. It used the generic WT sequence string helper, but the parser's memcpy length becomes garbage in this context and overwrites scene/global state.
  - `6/13` and `6/14` must stay out of the first scene-enter dispatch window until their exact inner blob encoding and safe timing are recovered.
- active experiment:
  - superseded by the next result below.

### 2026-06-08 25/5 same-window negative result

- confirmed from the latest operator rerun:
  - the safe 10-object packet is accepted: `mock_group_type1_response ... objects=10 ... len=633`.
  - dispatch order is `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `12/1`, `7/42`, `6/1`, `25/5`, `27/4`.
  - `case12_1` and `case27_4` both execute, and `27/4` leaves `R9+0x5C67/0x5C68` at `1,1`.
  - there is no crash and actor state remains stable (`actorId=10001`, valid draw callback), but the client still sends `WT len=49` at tick 134 for `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10`, and `25/5`.
  - the scene `loading.gif` owner still draws through `sub_1013C8A()` with `sceneGate=1,1 overlayCounter=0`.
- implication:
  - `25/5` is not the missing first-window object that suppresses WT49.
  - the remaining candidates are the `27/11` state event, same-window `2/10 otherinfo`, or correctly encoded/timed `6/13/6/14`.
- active experiment:
  - superseded by the next result below.

### 2026-06-08 27/11 plus 27/4 negative result

- confirmed from the latest operator rerun:
  - the 10-object packet is accepted: `mock_group_type1_response ... objects=10 ... len=621`.
  - dispatch order is `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `12/1`, `7/42`, `6/1`, `27/11`, `27/4`.
  - `case27_11` and `case27_4` both execute.
  - without a preceding `27/12`, `case27_11` writes `R9+0x5C67` as `1 -> 1`; `case27_4` then writes `R9+0x5C67/0x5C68` as `1,1`.
  - the client still sends `WT len=49` at tick 150, and the `sub_1013C8A()` / `loading.gif` owner remains active.
- implication:
  - `27/11` by itself is not the state event that suppressed WT49 in the earlier `27/12+27/11` run. The preceding `27/12(result=1, fb=1, name, posinfo)` is likely required because it seeds the fb/name/posinfo state consumed by `case27_11`.
- active experiment:
  - `src/main.c` now keeps the first-window object count at 10 by omitting `30/7` room roles by default while keeping `30/3` room NPC.
  - the new hypothesis packet is `5/10`, `10/26`, `30/1`, `30/3`, `12/1`, `7/42`, `6/1`, `27/12`, `27/11`, `27/4`.
  - hypothesis: this tests the full fb-target trio plus skill/book/taskinfo in the same dispatch window without using the known-unsafe `2/10` or `6/13`.

### 2026-06-08 full fb-target trio with 30/3-only room list negative result

- confirmed from the latest operator rerun:
  - the 10-object first-window packet is accepted with `mock_group_type1_response ... sceneRoomNpc=1 sceneRoomRoles=0 ... len=558`.
  - `trace_business_dispatch_item` shows the exact order `5/10`, `10/26`, `30/1`, `30/3`, `12/1`, `7/42`, `6/1`, `27/12`, `27/11`, `27/4`.
  - `27/12` reaches `net_handle_fb_target_dispatch()` at `0x01010BC0`.
  - `net_handle_business_followup_events()` reaches `case27_11` at `0x010106D8` and `case27_4` at `0x01010706`.
  - gate writes match the static parser expectation:
    - `0x010106F4`: `R9+0x5C67` `1 -> 0`
    - `0x0101087E`: `R9+0x5C67` `0 -> 1`
    - `0x01010882`: `R9+0x5C68` `1 -> 1`
- confirmed negative result:
  - the large scene-resource follow-up request still appears at tick 591: `WT len=49`, objects `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10`, `25/5`.
  - the mock standalone response is delivered at tick 592, but `net_business_response_dispatch()` sees `dispatchGate=0` and exits via `early_gate_off`.
  - the steady visible panel remains the `sub_1013C8A()` / `loading.gif` owner with `sceneGate=1,1` and `overlayCounter=0`.
- corrected interpretation:
  - the fb-target trio is confirmed structurally useful for gate transitions, but it does not by itself satisfy the client's post-enter resource wait in this 30/3-only arrangement.
  - `30/7` room roles is now a live first-window candidate because the prior successful-looking `27/12 + 27/11` run included room roles and the latest negative run omitted it.
- active experiment:
  - `src/main.c` now swaps the default room-list slot: omit `30/3` room NPC and include `30/7` room roles.
  - expected first-window order is `5/10`, `10/26`, `30/1`, `30/7`, `12/1`, `7/42`, `6/1`, `27/12`, `27/11`, `27/4`.
  - hypothesis: if WT49 disappears only with `30/7`, the room roles parser state is part of the readiness condition; if it persists, the remaining blockers are likely the unsafe/mismatched `6/13`/`6/14` or `2/10` encodings.

### 2026-06-08 30/7-only room list with full fb-target trio negative result and tasktypes correction

- confirmed from the latest operator rerun:
  - the 10-object first-window packet is accepted with `mock_group_type1_response ... sceneRoomNpc=0 sceneRoomRoles=1 ... len=528`.
  - dispatch order is `5/10`, `10/26`, `30/1`, `30/7`, `12/1`, `7/42`, `6/1`, `27/12`, `27/11`, `27/4`.
  - `30/7` reaches `sub_1039430()` at `0x01039430`.
  - `case27_11` and `case27_4` perform the same confirmed gate transition sequence: `R9+0x5C67` `1 -> 0 -> 1`, `R9+0x5C68` stays `1`.
  - the client still sends the large `WT len=49` request, and the standalone response is still blocked by `dispatchGate=0`.
- implication:
  - neither `30/3` nor `30/7`, individually paired with the fb-target trio, satisfies the first scene-resource wait.
  - the remaining first-window candidates move back to the requested `6/13`/`6/14` task-list pair and, later, a safe form of `2/10 otherinfo`.
- static correction for `6/13.tasktypes`:
  - `stream_reader_init_from_blob()` (`0x01033B16`) installs these read methods:
    - slot `+0x28`: `stream_read_i8_tagged()` (`0x01033AAC`), consuming `00 01 VV`
    - slot `+0x24`: `stream_read_i16_be_tagged()` (`0x01033A3A`), consuming `00 02 HH LL`
    - slot `+0x2C`: `stream_peek_i16_be()` (`0x01033A1E`), reading a raw big-endian string length without advancing
    - slot `+0x1C`: `stream_read_cstr_len16()` (`0x01033A86`), consuming `len16 + bytes`
  - case `13` at `0x01047896` therefore expects the `tasktypes` field value to begin directly with six records shaped as `tagged i8 + len16 C string`.
  - the previous mock used `vm_net_mock_put_object_blob()`, which prepended an extra raw `dataLen` word before those records. That was one wrapper mismatch.
  - the next trace then corrected the per-record scalar too: because `R6` is already `stream+0x400`, `[R6+0x28]` is `stream_read_i8_tagged()`, not `stream_read_i16_be_tagged()`. The old `00 02 00 00` scalar left the cursor at byte 3 and caused the next string length to be read from the wrong byte.
- active experiment:
  - `src/main.c` now emits `tasktypes` with `vm_net_mock_put_object_raw()` and per-record `tagged i8` tasktype ids.
  - the first-window bundle is now `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `12/1`, `7/42`, `6/1`, corrected `6/13`, and `6/14`.
  - hypothesis: corrected `6/13` should no longer corrupt the next per-item callback; if WT49 persists, the next missing requested object is likely `2/10 otherinfo`.

### 2026-06-08 tasktypes tagged-i8 positive result and empty `2/10` experiment

- confirmed from the newest manual run:
  - the first-window packet is accepted: `mock_group_type1_response ... objects=10 ... len=659`.
  - dispatch order is `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `12/1`, `7/42`, `6/1`, `6/13`, `6/14`.
  - `6/13` no longer corrupts the next per-item callback. `trace_tasktypes_case13_stream` shows the stream cursor advancing through six empty records and ending at cursor `36`; the pending `memcpy` length is `1` for each empty C string.
  - dispatch reaches `kind=6 subtype=14` at index `9`.
  - stdout has no fault in this run.
- confirmed negative result:
  - the client still sends the large `WT len=49` request at tick `147` for `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10`, and `25/5`.
  - the late `builtin-scene-resource-followup` is still blocked by `net_business_response_dispatch()` with `dispatchGate=0`.
  - the visible progress/fragments still come from `trace_loading_gif_widget_draw_entry ... caller=01013cba ... sceneGate=1,1 overlayCounter=0`.
- corrected interpretation:
  - `6/13.tasktypes` is now a confirmed parser match for the empty-tasktype shape: raw `tasktypes` field, six records of tagged i8 plus len16 C string.
  - `6/13 + 6/14` are not sufficient by themselves to satisfy the first-window scene-resource wait.
  - the previous non-empty same-window `2/10` crash should be attributed to the unconfirmed per-entry record shape or timing, not to the `2/10` object header itself.
- active experiment:
  - `src/main.c` now returns `1/2/10 {othernum=0, otherinfo=<empty>}` for both bundled and standalone `2/10`.
  - the first-window bundle keeps the accepted 10-object limit by defaulting `30/3 room NPC` off and preserving `30/7 room roles`.
  - expected next order is `5/10`, `10/26`, `30/1`, `30/7`, `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10(empty)`.
  - next verification should check whether WT49 disappears or changes, whether the adjacent protagonist-image fragments improve, and whether top-center map text returns or remains absent.

### 2026-06-08 empty `2/10` result and exact WT49 first-window experiment

- confirmed from the newest manual run:
  - the first-window packet is accepted: `mock_group_type1_response ... objects=10 ... sceneRoomNpc=0 sceneRoomRoles=1 ... len=536`.
  - dispatch order is `5/10`, `10/26`, `30/1`, `30/7`, `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10`.
  - empty `2/10` is parser-safe in this position; no stdout fault occurs and `trace_scene_current_node_draw` continues to report actor `10001` with valid callbacks.
- confirmed negative result:
  - the client still sends the large `WT len=49` request at tick `304`.
  - the late response is still blocked by `dispatchGate=0`.
  - the scene `loading.gif` widget still draws steadily from `sub_1013C8A()` with `sceneGate=1,1 overlayCounter=0`.
- implication:
  - empty `2/10` object presence is not sufficient to satisfy the first scene-resource wait.
  - after this run, the only object from the WT49 family that was not present in the open first-window dispatch was `25/5`.
  - because prior same-window `25/5` without `6/13/6/14/2/10` was also negative, the next experiment must test the exact seven-object family together rather than treating `25/5` alone as meaningful.
- active experiment:
  - defaults now omit both room-list objects (`30/3` and `30/7`) to keep the 10-object accepted limit.
  - first-window response now bundles the exact WT49 object family after the three base objects:
    - `5/10`, `10/26`, `30/1`, `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10(empty)`, `25/5`.
  - if WT49 still appears after this exact same-window coverage, the readiness condition is not just “all requested object headers were seen”; the next suspects become room-list side effects or non-empty semantic payloads.

### 2026-06-08 exact WT49 first-window negative result and misc-player bundle experiment

- confirmed from the newest manual run:
  - the first-window packet is accepted with `mock_group_type1_response ... objects=10 ... sceneRoomNpc=0 sceneRoomRoles=0 bundledSceneFollowup=1 ... len=424`.
  - `net_business_response_dispatch()` reaches the exact same-window WT49 family: `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, empty `2/10`, and `25/5`.
  - `6/13.tasktypes` remains parser-matched (`tagged i8 + len16 C string`, cursor ending at `36`), and empty `2/10` does not corrupt the protagonist node.
  - the current node remains valid in `scene_draw_actor_pass`: actor `10001`, grid `223,382`, `callback=drawAt=0x01045579`.
- confirmed negative result:
  - the client still emits the later `WT len=49` request.
  - the late standalone `builtin-scene-resource-followup` is still blocked by `dispatchGate=0`.
  - the steady progress strip remains the scene `loading.gif` owner path: `sub_1013C8A()` sees `sceneGate=1,1` and manager count `R9+0x9590 == 0`.
- corrected interpretation:
  - same-window presence of the exact requested object headers is not sufficient to complete the scene-resource wait.
  - two nearby scene-bootstrap misc-player replies are still not being consumed in the current sequencing: client requests `1/7/7 type=2` and `type=3` immediately after the bundled `type=1`, but their standalone responses arrive after `30/1` closes the scene object's dispatch gate.
  - static parser evidence: `net_handle_misc_player_fields()` at `0x01011C88` maps subtype `20` to `pcimg` and subtype `32` to `expcard`; both then flow through the common `kind=7` post-handler.
- active builder experiment:
  - `vm_net_mock_build_group_type1_response()` now bundles `7/20 {result=1, pcimg=0}` and `7/32 {result=1, expcard=0}` in the open first-window response before `30/1`.
  - evidence marker for the next run: `mock_group_type1_response ... bundledType2Type3=1 ... objects=12`.
  - hypothesis: if this changes the loading/fragments or suppresses WT49, then the blocked `1/7/7 type=2/3` replies are part of the normal scene-ready contract. If it does not, the remaining candidates shift back to semantic payload content and resource-manager/local descriptor readiness rather than raw object presence.

### 2026-06-08 12-object first-window unpack error and deferred scene-enter experiment

- confirmed from the newest manual run:
  - the 12-object first-window response is not parser-valid on this path: `net_business_response_dispatch()` enters with `responseLen=484`, then logs `unpack_error` with `r0=3` and `objectCount=10`.
  - because the invalid first packet never dispatches its embedded `30/1`, the scene object's dispatch gate remains open.
  - the later standalone `7/20.pcimg`, `7/32.expcard`, and the seven-object scene-resource response are then consumed successfully.
- confirmed negative result:
  - consuming those later objects still does not stop the `sub_1013C8A()` / `loading.gif` owner.
  - this run cannot prove the first-window 12-object semantic idea because the packet failed before object dispatch.
- corrected protocol constraint:
  - keep business response objects at 10 or fewer for this unpacker path unless static recovery proves a larger table is valid under different setup.
  - `30/1` remains dangerous early because it closes `sceneObj+0x164` before the client can consume the later requested scene-resource objects.
- active builder experiment:
  - first group/type1 response now stays small: `5/10`, `10/26`, `7/20`, `7/32`.
  - the scene-resource follow-up response now contains the requested seven-object WT49 family followed by trailing `30/1 scene+posinfo`.
  - hypothesis: this ordering lets the client consume group/misc fields, then requested resource fields, then close/enter the scene in a single valid follow-up window.

### 2026-06-08 valid deferred `30/1` ordering and remaining local scene wait

- confirmed from the newest manual run:
  - the small first group/type1 response is valid and dispatches exactly `5/10`, `10/26`, `7/20.pcimg`, and `7/32.expcard`.
  - the later scene-resource response is also valid and dispatches `12/1`, `7/42`, `6/1`, corrected `6/13.tasktypes`, `6/14`, empty `2/10.otherinfo`, `25/5`, then trailing `30/1.scene+posinfo`.
  - `30/1` clears `sceneObj+0x164` at `0x01039766` only after the seven requested WT49-family objects dispatch.
- packet-shape conclusion:
  - this validates the current mock's field ordering for the tested empty/minimal scene-resource family:
    - `6/13.tasktypes`: raw field payload, six records of `tagged i8 + len16 C string`.
    - `2/10.otherinfo`: parser-safe empty object with `othernum=0` and empty `otherinfo`.
    - `30/1.posinfo`: two tagged i16 coordinates.
  - no `unpack_error` or `early_gate_off` appears for this shape.
- remaining non-packet evidence:
  - the client still draws the scene loading widget from `sub_1013C8A()` because `R9+0x9590 == 0` and the two scene gate bytes remain `1`.
  - no resource-manager enqueue trace fired in this run, so the outstanding readiness condition currently looks local to scene descriptor/resource scheduling rather than a missing network response.
  - the actorinfo tail scene key remains a live hypothesis: `parse_actorinfo_response()` copies the second tail string into `R9+0x5E46`, and `scene_rebuild_runtime_nodes()` later pairs that value with hardcoded mode `10` and callback `sub_100F094`.

### 2026-06-08 actorinfo scene-key collision experiment

- static evidence:
  - `parse_actor_motion_descriptor()` calls `sub_100D534(name)` at `0x0100D6FC`.
  - if that local resource open succeeds, the function parses the returned stream and directly calls callback `a8` at `0x0100DB32`.
  - if that open fails, control branches through `0x0100D7D2` to `0x0100DB4A`, where it calls the `R9+0x9588` queue writer (`sub_10365F0`) with record type `6` and callback-choice byte `(a8 != 0)`.
- protocol/mock implication:
  - sending actorinfo's second tail string as the real local GBK `.sce` key currently makes the first descriptor pass parse portal data as a `sub_100F094` stream.
  - the active experiment changes only that c-key default to ASCII `c00PenglaiXiandao_01`, which is still c-prefixed but does not collide with an on-disk `.sce` in the current resource set.
  - the resource-update mock now serves the existing minimal motion-descriptor wrapper for that key if the client requests it.
- status:
  - hypothesis until the next runtime log proves whether the client now enters the queue/update path and whether the loading/fragments change.

### 2026-06-08 ASCII descriptor update path and WT39 resource subset

- confirmed runtime result:
  - `trace_update_request_prepare ... name=c00PenglaiXiandao_01 type=6 start=0 version=0` appears after the ASCII actorinfo scene-key experiment.
  - `mock_update_chunk_response ... chunk=44 totalsize=44` serves the minimal motion-wrapper payload, and `storage_trace.log` shows `MMORPGTempbin` renamed to `JHOnlineData/c00PenglaiXiandao_01`.
  - `parse_actor_motion_descriptor()` then opens `c00PenglaiXiandao_01`, `ui_h_war.actor`, and `h_warriorwalk2.gif`; the bytes handed to `sub_100F094()` are therefore no longer the local GBK `.sce` portal stream.
  - the minimal wrapper path currently yields `trace_actor_move_entry_table ... count=0 promptName=<null> statusText=<null>`, so it avoids the old portal body-entry but does not yet supply map-title/status semantics.
- confirmed packet consequence:
  - after the separate `12/1 + 7/42` request and the `18/7 type=6` update response, the client sends a smaller follow-up:
    - `WT len=39 hdr=6/1 objs=1/6/1,1/6/13,1/6/14,1/2/10,1/25/5 count=5`
  - older `src/main.c` only recognized the full `WT len=49` family and asserted on this subset.
- current mock behavior:
  - `builtin-scene-resource-followup` now accepts both shapes:
    - full `WT49`: `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, empty `2/10`, `25/5`
    - post-update `WT39`: `6/1`, `6/13`, `6/14`, empty `2/10`, `25/5`
  - the response mirrors only the requested skill/book presence, then appends trailing `30/1 scene+posinfo` to close/enter after the resource-family objects dispatch.
- interpretation:
  - the latest loading/progress symptom cannot be classified as purely local widget residue yet, because the newest run ended on a confirmed unhandled WT request.
  - actor-adjacent fragments in the next run should be evaluated after this WT39 response is consumed; the old `.sce` portal-entry root cause is no longer confirmed on the ASCII-key path.

### 2026-06-08 ASCII descriptor rollback

- confirmed after the next manual run:
  - the previously downloaded `JHOnlineData/c00PenglaiXiandao_01` was cached, so the client skipped `18/7 type=6` and opened that 44-byte descriptor directly.
  - the visible sprite fragments disappeared, but the map viewport became black.
  - storage evidence shows no open of local `c00蓬莱仙岛_01.sce` in that run; only the cached ASCII descriptor was used for the mode-10 descriptor path.
  - packet evidence remains good: the WT49 follow-up is consumed as eight objects, with trailing `30/1` closing the scene dispatch gate after `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, empty `2/10`, and `25/5`.
- conclusion:
  - the non-colliding ASCII key proves that the portal/body-entry artifact came from the local `.sce` descriptor path, but it also proves the local `.sce` path is needed for normal map/background initialization.
  - therefore the default mock should not avoid the concrete scene descriptor. The better current policy is to keep actorinfo's scene key aligned with `30/1.scene`, then prevent only the confirmed portal-shaped body/world entry from being published while more precise descriptor semantics are recovered.
- current mock state:
  - actorinfo's scene key default is back to `vm_net_mock_default_scene_name()`; `CBE_SCENE_KEY` remains an explicit override for ASCII/update-path experiments.
  - WT49 and WT39 scene-resource follow-up handling remains parser-safe and should continue to be validated in later runs.

### 2026-06-08 WT49 fb-target seed experiment

- latest confirmed correction:
  - with the default GBK scene key restored, the map background and title return, and storage trace again opens `JHOnlineData/c00蓬莱仙岛_01.sce`.
  - the current actor-adjacent artifact is not a published body/world move-entry: the narrow portal guard skips the known portal-shaped candidate and `trace_actor_move_entry_table` reports `count=0`.
  - the remaining artifact is the scene `loading.gif` owner path from `sub_1013C8A()`, evidenced by repeated `trace_loading_gif_widget_draw_entry ... caller=01013cba ... sceneGate=1,1 overlayCounter=0`.
- active mock experiment:
  - superseded first attempt: full WT49 scene-resource follow-up response appended `27/12(result=1, fb=1, name=sceneKey, posinfo=spawn)` and empty `27/11` before trailing `30/1`.
  - runtime result: the 10-object packet was accepted and `case27_11` executed, but it immediately produced the expected `sceneTickGate=0,1` branch and repeated `sub_1003568 -> sub_100337C(0)` calls.
  - corrected experiment: full WT49 scene-resource follow-up response now appends `27/12(result=1, fb=1, name=sceneKey, posinfo=spawn)` only, before trailing `30/1`, so the client can request the later `27/11 + 27/4` finalize chain itself.
  - full WT49 response order is now:
    - `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, empty `2/10`, `25/5`, `27/12`, `30/1`
  - WT39 subset responses remain limited to the requested task/other/info family plus trailing `30/1`.
- status:
  - `confirmed negative`: proactive `27/11` is not a safe WT49-tail object by itself; it clears gate A to 0.
  - superseded `hypothesis`: `27/12` alone would make the client drive the later `27/11 + 27/4` exchange via its own follow-up request.

### 2026-06-08 WT49 `27/12`-only negative and non-empty `27/4` experiment

- confirmed runtime result from the newest manual run:
  - the full WT49 resource response is accepted as 9 objects: `12/1`, `7/42`, `6/1`, corrected `6/13`, `6/14`, empty `2/10`, `25/5`, `27/12`, trailing `30/1`.
  - `net_handle_fb_target_dispatch()` consumes `27/12`: `trace_business_handler pc=01010bc0 packet=05001668 kind=27 subtype=12`.
  - no later `WT len=34`-class request appears, and no `mock_type27_followup_combo_response` appears.
  - the steady widget owner remains `trace_loading_gif_widget_draw_entry ... caller=01013cba ... sceneGate=1,1 overlayCounter=0`.
- static reason for the next field experiment:
  - `net_handle_business_followup_events()` case `27/4` at `0x01010706..0x01010882` reads `min`, `result`, `type`, `fb`, and `info`.
  - its simple success path writes `R9+0x5C67=1` and `R9+0x5C68=1`; when `info` is non-empty it also takes extra scene-object info side effects before those gate writes.
- current mock experiment:
  - full WT49 responses now append `27/12(result=1, fb=1, name=sceneKey, posinfo=spawn)` and `27/4(result=1, type=1, fb=1, info=<scene title>)`, then trailing `30/1`.
  - this deliberately omits `27/11`, because the latest accepted `27/12 + 27/11` WT49-tail run proved it enters `sceneTickGate=0,1`.
  - evidence marker for the next run: `mock_scene_resource_followup_response ... fbSeed12Ready4Info=1`.
  - hypothesis: if the missing semantic is the non-empty `27/4.info` branch, the next run should show `case27_4` without `case27_11` and should change the `sub_1013C8A()` loading-widget condition or subsequent request sequence.

### 2026-06-08 WT49 `27/12 + 27/4(info)` result and full trio experiment

- confirmed runtime result:
  - the 10-object WT49 response with `27/12 + 27/4(info)` is parser-valid and dispatches both primary handlers:
    - `trace_business_handler pc=01010bc0 ... kind=27 subtype=12`
    - `trace_business_handler pc=01010bc0 ... kind=27 subtype=4`
  - follow-up scanning reaches `case27_4` without `case27_11`.
  - the non-empty `info` branch is no longer just theoretical: the run visibly pops the map-name prompt, and trace shows the `case27_4` path allocating/copying the info string before writing `R9+0x5C67/0x5C68` to `1,1`.
- confirmed negative:
  - `case27_4` without `case27_11` does not change the `sub_1013C8A()` loading-widget condition. The tail remains `trace_loading_gif_widget_draw_entry ... sceneGate=1,1 overlayCounter=0`.
  - the actor/body table remains empty (`trace_actor_move_entry_table ... count=0`), so the visible actor-adjacent tiles are still the loading/progress skin, not body/world actor draw.
- new sequencing evidence:
  - `27/12` already calls `ui_apply_named_posinfo_target_with_fb()` and clears the scene business dispatch gate at `0x0100EA6A`.
  - in this packet shape, trailing `30/1` then only writes the same gate to `0` again at `0x01039766`.
- current mock experiment:
  - full WT49 responses now keep the 10-object limit by replacing trailing `30/1` with `27/11`.
  - full WT49 tail is now `27/12(result=1,fb=1,name,posinfo)`, `27/11`, `27/4(result=1,type=1,fb=1,info=<scene title>)`.
  - WT39 subset responses still append trailing `30/1` and do not use the fb-target trio.
  - evidence marker for the next run: `mock_scene_resource_followup_response ... fbFull12_11_4Info=1 trailingSceneEnter=0`.
  - hypothesis: the complete trio may need to run in one follow-up scan after the exact WT49 family; `27/4` should repair the `sceneGateA=0` side effect from `27/11` in the same pass.

### 2026-06-08 WT49 full trio negative and `2/1 moveinfo` upload ack

- confirmed runtime result:
  - the newest manual run hit the intended full-trio marker: `mock_scene_resource_followup_response objects=10 ... fbFull12_11_4Info=1 trailingSceneEnter=0 len=390`.
  - primary dispatch consumed all three fb-target objects:
    - `trace_business_handler pc=01010bc0 ... kind=27 subtype=12`
    - `trace_business_handler pc=01010bc0 ... kind=27 subtype=11`
    - `trace_business_handler pc=01010bc0 ... kind=27 subtype=4`
  - follow-up scanning reached `case27_11` and then `case27_4`; trace shows `case27_11` writing `R9+0x5C67` to `0`, then `case27_4` restoring `R9+0x5C67/0x5C68` to `1,1`.
- confirmed negative:
  - the full trio still does not satisfy the `sub_1013C8A()` loading-owner condition. Tail trace remains `trace_loading_gif_widget_draw_entry ... caller=01013cba ... sceneGate=1,1 overlayCounter=0`.
  - without the former trailing `30/1`, `runtime_state ... sceneState=1` is observed instead of the earlier `sceneState=7`; the map still draws, but this is a live sequencing difference to preserve as evidence.
- new packet evidence:
  - after manual click/move, the client emits `WT len=32 hdr=2/1 objs=1/2/1 count=1` with a `moveinfo` field: payload hex `57540020010201001c086d6f7665696e666f000c000a01000000000000000000`.
  - the previous mock had no handler and asserted on `unhandled_packet`.
- static parser evidence:
  - IDA `net_handle_actor_move_info()` at `0x01012ADC` reads subtype from `[R0+8]`, executes `SUBS R1,#2` at `0x01012AFA`, and subtype `1` falls through the default return at `0x01012B0C`.
  - the branch that actually reads field `moveinfo` starts at `0x01012B2E` and is case subtype `2`, with comments around `0x01012B6A..0x01012B76` for `moveInfo`, `gridPosY`, `moveInfoLen`, `actorId`, and `gridPosX`.
- current mock behavior:
  - `builtin-actor-moveinfo-ack` now recognizes a one-object `1/2/1` request containing `moveinfo` and returns an empty `1/2/1` object.
  - status: `confirmed parser-safe by static branch evidence`; semantic meaning of live movement upload remains `hypothesis`.

### 2026-06-08 WT49 full trio plus restored `30/1` experiment

- confirmed from the latest screenshot/log pair:
  - map background, top title, and protagonist draw are normal.
  - no `2/1 moveinfo` upload appears in this run, so `builtin-actor-moveinfo-ack` was not exercised.
  - the actor-adjacent artifact remains exactly aligned with `trace_loading_gif_widget_draw_entry ... caller=01013cba ... args=20,188,200`.
  - IDA `sub_1013C8A()` at `0x01013C8A` draws that widget only when `s16[R9+0x9590] <= 0` and `R9+0x5C67/+0x5C68` are both nonzero; runtime logs show `overlayCounter=0 sceneGate=1,1`.
- sequencing correction:
  - the prior full-trio packet omitted trailing `30/1`, and runtime reported `sceneState=1` instead of the earlier `sceneState=7` seen when `30/1` was present.
  - because the full trio alone is now confirmed negative, the next packet-shape experiment restores `30/1` while staying within the observed 10-object limit by omitting `25/5`.
- current mock experiment:
  - full WT49 response order is now `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, empty `2/10`, `27/12`, `27/11`, `27/4(info)`, trailing `30/1`.
  - evidence marker for the next run: `mock_scene_resource_followup_response ... result25_5=0 fbFull12_11_4Info=1 trailingSceneEnter=1`.
  - hypothesis: if the remaining issue is the missing scene-enter finalization after the fb-target trio, this should restore the `30/1` side effects and change `sceneState` and/or the persistent `sub_1013C8A()` widget condition.

### 2026-06-08 WT49 restored `30/1` negative and runtime moveinfo ack

- confirmed runtime result:
  - the restored full WT49 response is parser-valid: `mock_scene_resource_followup_response objects=10 ... result25_5=0 fbFull12_11_4Info=1 trailingSceneEnter=1 len=420`.
  - `27/11`, `27/4(info)`, and trailing `30/1` are all consumed; the post-data snapshot returns to `sceneState=7` with `sceneGate=0` and `loadFlags=0,0,0`.
  - after manual movement, the client sends `WT len=32 hdr=2/1 objs=1/2/1` carrying `moveinfo`, and `builtin-actor-moveinfo-ack` returns an empty `1/2/1` response.
- confirmed negative:
  - restoring trailing `30/1` after the fb-target trio does not clear the scene loading widget: repeated runtime traces remain `trace_loading_gif_widget_draw_entry ... caller=01013cba ... sceneGate=1,1 overlayCounter=0`.
  - the runtime-exercised `2/1 moveinfo` ack does not change the widget condition or produce a new unhandled packet. The ack response is delivered after the scene business dispatch gate is closed and therefore logs `early_gate_off`; this is consistent with static `net_handle_actor_move_info()` evidence that subtype `1` falls through the default return at `0x01012B0C`.
  - no resource-manager enqueue trace or `R9+0x9590` write appears in this run, so the `R9+0x9588` queued-entry count stays `0`.
- protocol implication:
  - for the currently tested shape, the missing field is no longer likely to be simple WT49 tail ordering among `25/5`, `27/12`, `27/11`, `27/4(info)`, and `30/1`.
  - the next protocol/runtime boundary is the manager/replay contract behind `R9+0x9588` and the local descriptor path that pairs `currentMapIdText=c00蓬莱仙岛_01`, `mode=10`, and `callback=sub_100F094+1`.

### 2026-06-08 descriptor/manager trace points for next run

- trace-only instrumentation added after the above negative:
  - `trace_actor_motion_open_result` at `0x0100D702` records whether `parse_actor_motion_descriptor()` takes the local-open-success/direct-parse path or the local-open-fail/enqueue path after `sub_100D534(name)`.
  - `trace_actor_motion_enqueue_fallback` at `0x0100DB4A` / `0x0100DB74` records the fallback and final manager-writer call context if a descriptor is not opened locally.
  - `trace_manager_replay_entry` at `0x01036768` records the `R9+0x9588` current record before `scene_hud_main_panel_sync_message()` replays it into `scene_hud_main_panel_init()`.
- intended evidence split:
  - `local_open_success_direct_parse` for the GBK `.sce` key would support the hypothesis that the persistent loading/progress widget is driven by local descriptor/callback semantics rather than an outstanding server response.
  - fallback/enqueue evidence would reopen the question of a missing `18/7 type=6` resource response or wrong descriptor key policy.

### 2026-06-08 descriptor/manager trace result

- confirmed result:
  - default GBK key `c00蓬莱仙岛_01` opens locally in `parse_actor_motion_descriptor()` (`0x0100D6E2`): `trace_actor_motion_open_result ... branch=local_open_success_direct_parse`.
  - the fallback path to `get_net_manager_object()->sub_10365F0(type=6, ...)` is not taken on the default path, and `scene_hud_main_panel_sync_message()` (`0x01036768`) does not replay a `R9+0x9588` record.
  - static `sub_100D6E2` evidence matches this split: local-open success parses the `.sce` body and only then calls `a8(stream,cursor)` if the callback is non-null; local-open failure is the only branch that queues a type-6 manager record.
- implication:
  - current evidence does not support another simple `WT49` response-field mismatch as the root cause of the visible loading/progress widget.
  - the still-live boundary is local: the `.sce` tail is being handed to the hardcoded `sub_100F094` callback from `scene_rebuild_runtime_nodes()` (`0x0100F8E8..0x0100F8F6`), while `R9+0x9590` remains zero and `sub_1013C8A()` continues to draw the loading widget.

### 2026-06-08 message-box vs loading-widget trace split

- corrected evidence boundary:
  - `loading_gif_widget_draw_frame()` only performs the center-strip blit while widget byte `+0x10` is set. The newest manual run's later steady entries have `trace_loading_gif_widget_draw_entry ... flag10=0`, so those entries are no longer confirmed evidence of ongoing blitting.
  - the visible centered green `蓬莱-铜雀台` panel is now tracked as a separate message-box path, not as protocol-confirmed loading residue.
- new read-only trace for the next run:
  - `trace_scene_message_request` covers `sub_1013BDC()` call sites (`0x01013C28`, `0x01013C52`, `0x01013C84`), `sub_10110E6()` entry/branches (`0x010110E6`, `0x01011112`, `0x01011120`), and `sub_101037E()` queued-message insertion (`0x0101037E`).
  - expected interpretation: `message_box_branch` with map-title text supports a normal local prompt/modal path; `scene_widget_timeout_message` supports a still-missing local completion signal. Neither outcome should be treated as a new server response format until paired with parser evidence.

### 2026-06-08 actorinfo `previewImage` correction

- confirmed runtime/static mismatch:
  - latest runtime actorinfo snapshot: `preview=h_warriorwalk1.gif`, `actor=h_warrior.actor`, `scene=c00蓬莱仙岛_01`.
  - IDA final consumer `scene_draw_node_overhead_overlay()` (`0x01045578`) treats the `actor+0x10A` string as an optional overhead named badge/icon. It checks `sub_1000342(a1+266) > 0`, resolves the named asset through the shared actor/HUD asset pipeline at `0x010456AC`, and draws it at `0x01045762` or `0x01045834`.
- protocol implication:
  - the current mock's `previewImage` slot must not default to the actor walk GIF. That made body art eligible for overhead badge rendering, matching the visible actor-fragment artifact.
  - `actor+0xD8` remains the real `.actor` resource string used for the actor resource family, and is unchanged.
- current mock behavior:
  - `vm_net_mock_actor_preview_image_name()` now defaults to `""` and only sends a value when `CBE_ACTOR_PREVIEW_IMAGE` is explicitly set.
  - status: `confirmed parser-consumer mismatch`; visual disappearance still needs the next manual run for runtime confirmation.

### 2026-06-08 post-scene-change WT44 task subset

- observed request:
  - `WT len=44 hdr=25/5`
  - objects: `1/25/5`, `1/6/1`, `1/6/13`, `1/6/14`, `1/2/10`, `1/25/5`
  - final object carries `Type=101`.
- relationship to earlier WT49:
  - this is the already handled scene-resource follow-up family minus `12/1 learnedskill` and `7/42 booksinfo`.
  - it appears after a `2/3 maptype/mapID/exitID` scene-change exchange, so no scene key is available in the WT44 request itself.
- current mock behavior:
  - `builtin-scene-task-subset-followup` replies with parser-safe empty forms for `6/1`, `6/13`, `6/14`, `2/10`, and `25/5`.
  - corrected after the newest run: the WT44 response itself is too late for scene completion because the scene dispatch gate is already closed. It should not carry `30/1`; scene completion belongs in the preceding scene-change combo response.
- status:
  - `confirmed unhandled packet shape`
  - `confirmed parser-safe response fields by prior IDA/runtime evidence`
  - `confirmed`: scene-aware trailing `30/1` inside WT44 reaches `early_gate_off` and is not consumed.

### 2026-06-08 scene-change loading completion

- runtime evidence:
  - after `mock_scene_change_combo_response ... scene=00蓬莱仙岛_02.sce exitId=1 pos=128,45`, the client emits the WT44 task subset and receives `builtin-scene-task-subset-followup responseLen=177`.
  - because that response had no `30/1`, the client keeps `trace_loading_gif_widget_draw_entry ... flag10=1 gate5530=1 ... frame=189..300` and continues blitting the center strip through `trace_progress_strip_wrapper_tick ... caller=010460ca dst=20,188`.
  - `sub_1013BDC()` then hits the timeout path at `0x01013C84`, shown by `trace_scene_message_request label=scene_widget_timeout_message ... text=网络连接超时!`, and only that timeout clears `gate5530`.
  - a follow-up experiment moved scene-aware `30/1` into the WT44 response; runtime proved it is too late: `trace_business_dispatch_state label=early_gate_off ... dispatchGate=0 objectCount=6`.
  - a later run with `30/1` moved into `mock_scene_change_combo_response` confirms that object is consumed in the same dispatch window:
    - `mock_scene_change_combo_response objects=6 ... trailingSceneEnter=1 scene=00蓬莱仙岛_02.sce exitId=1 pos=128,45`
    - `trace_business_handler ... kind=30 subtype=1`
    - `runtime_state ... sceneGate=0 sceneState=7 loadFlags=1,0,0`
    - the subsequent WT44 response takes `early_gate_off`, then `runtime_state ... loadFlags=0,0,0`, but the visible strip still logs `trace_loading_gif_widget_draw_entry ... flag10=1 gate5530=1` and paired `trace_progress_strip_wrapper_tick`.
  - the next run with `R9+0x5530` write tracing identifies the exact late-WT44 failure:
    - when the client sends the post-scene-change WT44 request, `sub_103478E()` copies request-object byte `+5` into `R9+0x5530` at `0x01034796`, opening the loading strip.
    - the WT44 response is queued, but `net_business_response_dispatch()` immediately takes `early_gate_off` at `0x01012F42` because the prior combo `30/1` has closed `sceneObj+0x164`.
    - the wrapper then clears only `R9+0x5531` at `0x010348F6`; no parser handler runs to clear `R9+0x5530`, so the strip remains active.
- static evidence:
  - IDA `net_handle_scene_channel_dispatch()` (`0x01039B8A`) dispatches subtype `1` to `parse_scene_response_entry()` and subtype `2` to `parse_scene_posinfo_field()`.
  - this matches runtime from the successful first scene-resource follow-up: trailing `kind=30 subtype=1` is followed by `runtime_state ... sceneState=7 loadFlags=0,0,0` and loading-widget entries with `gate5530=0`.
- current builder:
  - `mock_scene_change_combo_response` records the latest scene-change target.
  - `mock_scene_change_combo_response` now appends `30/1` with that scene and spawn in the same dispatch window as `30/2`, e.g. `scene=00蓬莱仙岛_02.sce pos=128,45`.
  - confirmed negative experiment: for the observed second-map request shape (`27/11`, `27/4`, `7/42` but no `12/1`), pre-bundling the WT44 task subset before `30/1` at the 10-object practical limit was consumed but did not suppress the later WT44 request.
    - runtime marker: `mock_scene_change_combo_response objects=10 ... fb11empty=0 ... taskSubset=1 ... trailingSceneEnter=1`.
    - immediately afterward, the client still sent `WT len=44`, the response reached `net_business_response_dispatch()` with `dispatchGate=0`, and `early_gate_off` at `0x01012F42` left `R9+0x5530` set until the `sub_1013BDC()` timeout path.
  - current code has reverted that negative experiment; the scene-change combo again mirrors the requested scene-change family plus scene-aware trailing `30/1`.
  - `mock_scene_task_subset_followup_response` returns only parser-safe task/other/banner objects because its event is expected after `dispatchGate=0`.
- status:
  - `confirmed`: WT44 without `30/1` is insufficient and times out.
  - `confirmed`: WT44 plus scene-aware `30/1` is also insufficient because it is delivered after dispatch gate close.
  - `confirmed partial/negative`: scene-change combo plus scene-aware `30/1` restores the normal `30/1` parser side effect (`sceneState=7`) but is not sufficient by itself to clear the second-map loading strip.
  - `confirmed`: the stuck strip is caused by a late WT44 request opening `R9+0x5530` after the scene dispatch gate has closed, then receiving a response that cannot reach the clearing handlers.
  - `confirmed negative`: pre-bundling the WT44 subset in the scene-change combo does not suppress the late WT44 request and does not leave `R9+0x5530` clear after the combo.
  - `confirmed`: first-scene normal timing uses the same `sub_1013BDC()` wait callback (`R9+0x554C == 0x0103478F`). Before the call, `waitObjFlag10=7`; after `BLX`, callback return `R0=0x31`, `waitObjFlag10=0`, and the sent WT49 response dispatches before the scene gate closes, so handlers clear `R9+0x5530`.
  - `confirmed`: `R9+0x5564` is `eventObj+0x10`, the outgoing WT object count. On the second-map scene-change path, that count is incremented during combo consumption and then sent by `sub_1013BDC()`.
  - `static evidence`: `sub_1049188()` (`0x01049188`), reached from the `30/2` scene-change handler, calls `alloc_outgoing_game_event(2, 0, 25, 5)` at `0x01049192`, matching the first object in the late WT44 request.
  - `confirmed`: the late WT44 is not exclusively caused by `30/2`. The newest alloc trace confirms `30/2`/`sub_1049188()` creates the first `25/5` object, while IDA `scene_runtime_tick()` (`0x01014E54..0x01014EEE`) queues the later one-shot sync family: direct `6/1`, `6/13`, `6/14`, then `send_game_event_type(101)` (`2/10`) and `send_default_scene_event()` (`25/5`).
  - `confirmed`: in `scene_runtime_tick()`, `R4` is loaded from `R9+0x5C64`, so the one-shot gate `[R4+1]` is `R9+0x5C65`.
  - `confirmed`: `R9+0x5C65` is set by `scene_runtime_init_and_sync()` at `0x01013010` before the second-map scene-change response is dispatched. This makes the WT44 one-shot a normal runtime-init request, not a malformed parser side effect.
  - current builder experiment:
    - confirmed negative: deferring all scene completion and later appending `30/1` causes bounce-back to the previous map.
    - confirmed negative: keeping `30/2` in the scene-change combo prevents bounce-back, but `30/2` closes the dispatch gate at `0x0103980E`, so the one-shot WT44 response still reaches `early_gate_off`.
    - confirmed negative: deferring `30/2` itself into the one-shot response also causes bounce-back to the previous map.
    - current behavior: scene-change completion is restored to the immediate combo response; the one-shot subset response is parser-safe task/other/banner only.
    - unresolved: early `30/2` commits the target map but closes the business gate before the one-shot WT44 response, leaving `R9+0x5530` set. The fix likely requires a different response contract or local completion signal, not moving scene completion into WT44.

### 2026-06-08 scene-change `2/3 mapID/exitID` response correction

- confirmed runtime mismatch from the first portal transfer:
  - the client requested `1/2/3` with `mapID=00蓬莱仙岛_02.sce` and `exitID=1`.
  - the old mock replied with a fixed `30/2.scene=c00蓬莱仙岛_01` and default `posinfo=(223,382)`.
  - after consuming that response, runtime stayed on the loading screen with `sceneState=1`, `loadFlags=1,0,0`, and repeated `trace_loading_overlay_call ... overlayState=3`.
- local resource evidence:
  - `tools/inspect_sce.py bin/JHOnlineData/00蓬莱仙岛_02.sce` shows the matching `entry_id=1` portal spawn at `(128,45)`.
  - the same `_02.sce` file is the descriptor already opened locally by `parse_actor_motion_descriptor()` before the network `2/3` request is sent.
- current mock behavior:
  - `mock_scene_change_combo_response` derives `30/2.scene` and `27/12.name/posinfo` from the requested `mapID/exitID`.
  - for the observed `00蓬莱仙岛_02.sce + exitID=1`, it returns scene `00蓬莱仙岛_02.sce` and `posinfo=(128,45)`.
  - the combo response now also mirrors the requested follow-up family as `27/12`, then `27/11`, then `27/4`, plus `7/42` when requested, instead of sending `27/11` before seeding `27/12` and omitting `27/4`.

### 2026-06-09 scene-change split `30/2` experiment

- status: `confirmed partial positive`
- static evidence:
  - `parse_scene_posinfo_field()` (`0x01039770`) reads `result`, `scene`, then optional `posinfo`.
  - only the `result==1 && posinfo present` branch applies the scene/position and closes `sceneObj+0x164` at `0x0103980E`.
  - the function still calls `sub_10491AE()` at `0x0103993C` even when `posinfo` is omitted, and `sub_10491AE()` reaches `sub_1049188()` / `alloc_outgoing_game_event(2,0,25,5)`.
  - `27/12` uses `ui_apply_named_posinfo_target()` (`0x0100E9B8`), which also closes the same dispatch gate at `0x0100EA6A`; it cannot stay in the early scene-change combo if the goal is to let the later WT44 response dispatch.
- current mock experiment:
  - immediate scene-change combo sends `30/2` with `result`, `type`, and `scene`, but no `posinfo`.
  - the same combo keeps requested non-closing objects such as `27/11`, `27/4`, and `7/42`.
  - the post-scene-change WT44 task subset response appends `27/12`, `27/11`, `27/4`, then full `30/2 result/type/scene/posinfo` using the remembered scene-change target.
- expected markers:
  - `mock_scene_change_combo_response ... sceneChangeResult=1 trailingSceneEnter=0 deferredSceneCompletion=1`
  - `mock_scene_task_subset_followup_response ... deferredSceneChangeResult=1 deferredSceneCompletion=1 lateDispatchExpected=0`
- success criteria:
  - no immediate map request back to the previous `c00蓬莱仙岛_01` target.
  - the WT44 response enters `net_business_response_dispatch()` with the dispatch gate still open.
  - `R9+0x5530` does not remain set until the `scene_widget_timeout_message` branch.

Newest runtime result:

- confirmed:
  - the immediate scene-change combo with `30/2 result/type/scene` and no `posinfo` is enough to avoid the bounce-back seen when `30/2` was omitted entirely.
  - the post-scene-change WT44 response dispatches with `dispatchGate=1`, consumes the deferred `27/12`, `27/11`, `27/4`, and full `30/2 scene+posinfo`, then clears `R9+0x5530`.
- remaining issue:
  - after WT44 completion, a short empty `25/5` default-scene request is sent. The mock's `25/12 result=4` response is parser-safe, but the emulator was delivering this one response reentrantly through `queue_data_event_immediate_flush`.
  - runtime trace shows the reentrant response first clears `R9+0x5531` at `0x010348F6`, then the still-running send wrapper writes `R9+0x5531` back to `1` at `0x010347E8`.
  - with that gate stuck, later local movement requests append `2/1 moveinfo` objects until `eventObj+0x10` reaches cap 10; a later scene init then cannot enqueue its normal `2/3`, `27/11`, `27/4`, or `7/42` requests.
- current mock timing:
  - short `25/5` responses remain enabled as `25/12 result=4`, but they are now queued like normal async network data instead of being immediately flushed in the same call stack.
  - status: `confirmed timing correction`; this is a mock network timing fix, not a client global/state patch.

Follow-up runtime confirmation:

- no `queue_data_event_immediate_flush` marker appears for short `25/5`.
- repeated scene changes now continue past the previous queue-cap blocker:
  - `c00蓬莱仙岛_03.sce exitID=0` is requested at tick `4653`.
  - `00蓬莱仙岛_02.sce exitID=0` is requested at tick `4864`.
- no `count=10 cap=10`, `scene_widget_timeout_message`, `unhandled_packet`, or `assert(0)` marker appears in the run.
- the remaining post-movement `early_gate_off` entries come from empty `2/1 moveinfo` / `2/10` acks after scene dispatch has closed; they are not confirmed blockers because later scene-change sends still succeed.

### 2026-06-09 short `7/18` item/use request

- observed request:
  - `WT len=9 hdr=7/18 objs=1/7/18 count=1`
  - runtime source: `sub_102C2E8` sends `alloc_outgoing_game_event(10, 1, 7, 18)` at `0x0102C2FE` after a HUD/touch action, then writes `R9+0x7A28+3=1`, `R9+0x7A28+4=1`, and `R9+0x7A28+2=0`.
- parser evidence:
  - the main kind-7 handler `net_handle_misc_player_fields()` (`0x01011C88`) does not handle subtype `18`.
  - the matching UI-local response parser is `sub_102C104` (`0x0102C104`), which handles kind `7` subtype `17`, `34`, and `4`.
  - for subtype `17`, it reads fields in this order: `result`, `useinfo` blob/string, `pcimg`. It then shows `useinfo` on `result == 1`; otherwise it shows the local `网络异常!` string.
- current mock:
  - `builtin-item-use18` responds to short `7/18` with `1/7/17 { result=1, useinfo="OK", pcimg=0 }`.
- status:
  - response field order is `confirmed` from IDA.
  - end-to-end consumption is `hypothesis`: in the latest failing trace the scene business dispatch gate was already closed (`sceneObj+0x164 == 0`), so the next run must verify whether this response is consumed or still reaches `early_gate_off`.

Newest runtime result:

- confirmed:
  - `builtin-item-use18` removes the previous unhandled/assert path. `bin/logs/net_packets.log` now records `source=builtin-item-use18 responseLen=48 WT len=9 hdr=7/18 objs=1/7/18 count=1`.
  - the response bytes are a parser-shaped `1/7/17` object (`result=1`, `useinfo=OK`, `pcimg=0`), matching `sub_102C104` field order.
  - the response is not consumed on this run: it enters `net_business_response_dispatch()` at tick `226` with `dispatchGate=0` and exits through `early_gate_off` at `0x01012F42`.
  - only the wrapper block gate `R9+0x5531` is cleared at `0x010348F6`; `R9+0x5530` remains `1`, and the loading strip continues with `trace_loading_gif_widget_draw_entry ... flag10=1 gate5530=1`.
- implication:
  - the remaining problem is not a missing `7/18` mock response field. It is a delivery/dispatch sequencing problem for post-map HUD data while `sceneObj+0x164` is closed.
  - IDA `net_business_response_dispatch()` confirms that the scene-gate check occurs before packet unpacking/object iteration, so the UI-local `sub_102C104` parser cannot see the otherwise parser-shaped `7/17` object in this path.
- instrumentation:
  - `trace_business_dispatch_state` now also logs `R9+0x5D30` fallback callback plus the active `R9+0x5EF0` manager child/cb10, so the next run can distinguish a real fallback-channel contract from a normal object-loop response blocked by `early_gate_off`.

Newest correction after the practise-info panel run:

- confirmed:
  - the active manager child callback at the blocked response is `managerChildCb10=0x0102CB47`, i.e. `sub_102CB46`, not `sub_102C104`.
  - `sub_102CB46` handles kind `7` subtype `18` and reads fields in this order: `todaypasthour`, `todaypastmin`, `getexp`, `todaylasthour`, `todaylastmin`, `alllasthour`, `alllastmin`, `isgold`.
  - therefore the earlier `7/18 -> 7/17 { result/useinfo/pcimg }` response was a mismatch for the visible `修炼信息` panel.
- current mock:
  - `builtin-practise-info18` now responds with kind `7` subtype `18` and the confirmed practise fields, using zero current/today values and `alllasthour=2`.
  - the response remains on the normal business event `7` path. A narrow experiment queued this one response as event `6`, but the latest run proves that is not the correct transport contract.
- confirmed negative transport experiment:
  - packet log: `source=builtin-practise-info18 responseLen=151 WT len=9 hdr=7/18 objs=1/7/18 count=1`, and the response bytes contain the confirmed fields (`todaypasthour`, `todaypastmin`, `getexp`, `todaylasthour`, `todaylastmin`, `alllasthour`, `alllastmin`, `isgold`).
  - runtime trace: `queue_data_event ... event=6 len=151`, then `fire_event ... event=6`.
  - `net_business_response_dispatch()` is entered at tick `165` with `r3=6`, `dispatchGate=0`, `fallbackCb=05083027`, `managerChild=01058af8`, and `managerChildCb10=0102cb47`.
  - the dispatcher then logs only `fallback_exit`; no `trace_practise_info_parser` line appears.
  - therefore event `6` does not call the active manager-child parser `sub_102CB46` and does not clear `R9+0x5530`.
- remaining status:
  - confirmed: the `7/18` field shape is now aligned with `sub_102CB46`.
  - confirmed: fallback event `6` is not the delivery path for this response.
  - unresolved: the normal event `7` object loop is still blocked after map entry because `sceneObj+0x164 == 0`; the next investigation should target response ownership / scene dispatch-gate sequencing rather than changing the practise-info fields.

Follow-up numeric encoding correction:

- confirmed runtime evidence:
  - after the keep-gate response was fixed, the later `7/18` response entered `sub_102CB46`: `trace_practise_info_parser label=entry ... kind=7 subtype=18`.
  - the loading gate then cleared through the normal business-handler path, so the stuck progress strip disappeared.
  - however, the first `u16` encoding experiment displayed huge values; `after_fields` logged values such as `29807`, `26469`, `24940`, and `26995`, which correspond to ASCII/GBK-ish string fragments rather than the intended `0/2` minute/hour values.
  - `getexp`, which was encoded with the existing integer/u32 helper, remained sane.
- corrected mock behavior:
  - `builtin-practise-info18` now encodes all time fields with the normal integer/u32 object-value helper even though `sub_102CB46` stores them into 16-bit fields.
  - current synthetic values are:
    - `todaypasthour=0`, `todaypastmin=15`
    - `getexp=120`
    - `todaylasthour=1`, `todaylastmin=45`
    - `alllasthour=1`, `alllastmin=45`
    - `isgold=0`

Current first-scene gate experiment:

- evidence:
  - the first-scene WT49 response currently reaches the object loop while `sceneObj+0x164 == 1`.
  - in the newest trace, `27/12` closes that gate at `0x0100EA6A` before the later practise-info UI request; the trailing `30/1` then writes the already-zero gate again at `0x01039766`.
  - IDA confirms `ui_apply_named_posinfo_target()` (`0x0100E9B8`) closes the gate unconditionally, and `parse_scene_response_entry()` (`0x010396EA`) closes it when `posinfo` is present.
  - once closed, `net_business_response_dispatch()` checks the gate before packet unpacking, so the active manager-child parser cannot see later `7/18` responses.
- hypothesis under test:
  - the first-scene resource follow-up may be closing the business dispatch gate too early for post-map HUD/business responses.
- current mock experiment:
  - for the first-scene WT49 shape (`12/1 + 7/42` present), `mock_scene_resource_followup_response` defaults to `CBE_FIRST_SCENE_KEEP_BUSINESS_GATE=1`.
  - in that mode it omits the `27/12/27/11/27/4` fb-target trio and replaces the full `30/1 scene+posinfo` with a non-closing `30/2 result+scene` ack without `posinfo`.
  - this is a server-response experiment only; it does not patch the client gate or call the practise parser directly.
- next runtime markers:
  - positive: `mock_scene_resource_followup_response ... keepBusinessGate=1`, no close write at `0x0100EA6A/0x01039766` in the first-scene WT49 window, and later `7/18` reaches `trace_practise_info_parser`.
  - negative: map completion regresses, scene state/load flags stick, or the later `7/18` still reaches `early_gate_off`; in that case restore the old builder with `CBE_FIRST_SCENE_KEEP_BUSINESS_GATE=0` while continuing gate/ownership research.

### 2026-06-09 scene/menu `4/1` challenge interaction request

- observed request:
  - `WT len=60 hdr=4/1 objs=1/4/1(id=105) count=1`
  - fields: `id=105`, `index=1`, `posx=142`, `posy=310`
  - runtime trigger: map screen touch path (`activeScreen=01053f78`, `currentThis=01053f60`) after several handled `2/1 moveinfo` uploads; no loading widget gate was active (`gate5530=0` in the tail trace).
- static evidence:
  - `net_business_response_dispatch()` (`0x01012E4C`) routes kind `4` response objects to `net_handle_login_or_name_result()` (`0x0101258A`).
  - `net_handle_login_or_name_result()` handles subtype `14` by reading only `result` and mapping result values `2..7` to local duel/challenge failure messages; subtype `15`/`17` read `name`; subtype `1` is not a confirmed response parser path.
  - `sub_1037ED4()` (`0x01037ED4`) constructs the matching outgoing `1/4/1` object and writes `id`, `index`, `posx`, and `posy`. Its only direct static caller is `task_hall_activate_selected_entry()` action `13` (`0x01049370`), while the newest runtime touch also shows the scene/menu child callback family around `0x0101AA0B`.
- current mock:
  - `builtin-challenge-interaction` recognizes the exact single-object `1/4/1` request with `id/index/posx/posy`.
  - it returns `1/4/14 { result=4 }`, a parser-safe local failure path, instead of trying to start battle or mutate local state.
- status:
  - confirmed: outgoing request field names/order from runtime bytes and IDA `sub_1037ED4()`.
  - confirmed: response field order for the chosen failure path from IDA `net_handle_login_or_name_result()`.
  - hypothesis: the real success response may involve battle module setup or another packet family; that contract is not implemented yet.

Newest runtime result:

- confirmed:
  - the unhandled/assert path is gone. Packet log shows `source=builtin-challenge-interaction responseLen=23 WT len=60 hdr=4/1 objs=1/4/1(id=105) count=1`.
  - response bytes are the intended `1/4/14 { result=4 }`: `mock_challenge_interaction_response response=4/14 result=4 len=23`.
  - no `unhandled_packet`, `assert(0)`, `scene_widget_timeout_message`, `count=10 cap=10`, or immediate-flush marker appears in this run.
- caveat:
  - the response is delivered after the scene business gate has already closed (`runtime_state pre_data_event ... sceneGate=0` at tick `893`), so this run does not confirm that `net_handle_login_or_name_result()` consumed the `4/14` object.
  - the outgoing request still opens the local loading owner (`flag10=1 gate5530=1` around ticks `892..897`), and it clears only after the next movement upload/ack path. Treat the current `4/14` response as packet-layer coverage plus parser-shaped evidence, not as a confirmed success/clear contract.
