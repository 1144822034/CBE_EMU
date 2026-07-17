# Teleport Stone Phase

## Status

Implemented server-side handling for the teleport-stone request family:

- `1/16/1`: list teleport destinations.
- `1/16/2 + 1/16/3 [+ 1/7/1]`: consume the client's combined confirmed-exit
  request and return inventory acknowledgements plus main-business `30/1` for
  the saved destination.
- `1/16/4`: map-UI transfer preparation request; the response opens the
  client's normal confirmation path before `16/2` and `16/3` perform entry.

Runtime validation has covered the crash point and the first transfer request:

- `16/1` is now handled by `builtin-teleport-stone-list`.
- `16/2` is now handled by `builtin-teleport-stone-transfer`.
- `16/4` is now handled by `builtin-teleport-stone-map-confirm`.
- the first post-enter combo after `16/2` was observed as
  `27/11 + 12/1 + 7/42`; a narrow completion handler was added for it.

Build validation passes with `make`.

## Request Evidence

User crash report:

```text
[error][network] unhandled wt=16/1 len=9 objects=1 first=1/16/1:0 last_source=- last_resp=0
Assertion failed!
```

The project log labels `wt` from the first object's kind/subtype. The packet is
an empty object:

```text
object: major=1 kind=16 subtype=1 payload_len=0
```

IDA evidence:

- `mmGameMstarWqvga.cbm:sub_8A8(0x000008A8)`
- branch `a1 == 14` calls the registered game-event allocator as
  `event(5, 1, 16, 1)`, producing the empty teleport-list request.
- later selection code sends `16/2` and `16/3` through `sub_194E`.

## Response Contract

### `1/16/1`

Parser evidence:

- `mmGameMstarWqvga.cbm:sub_11CE(0x000011CE)`
- object kind `0x10`, subtype `1`
- reads raw field `exitinfo`
- stream reader order:

```text
u8 count
repeat count:
  u32 exit_id
  len16 string label
  u32 reserved_or_extra
```

Implementation:

```text
exitinfo:
  count = 1
  exit_id = 1
  label = "Penglai Home"
  reserved_or_extra = 0
```

`exitinfo` must be a raw object field, not a length-wrapped blob. This matches
the existing `iteminfo`/`posinfo` stream fields.

### `1/16/2`

Parser evidence:

- `mmGameMstarWqvga.cbm:sub_11CE(0x000011CE)` subtype `2`
- `result == 2` opens the confirmation callback path.
- `result == 4` shows a hint.
- Any other result falls through to `sub_BCC()` and performs scene entry.

The older non-map implementation follows the confirmation branch for `16/2`,
and includes the selected target fields:

```text
result = 2 as typed-u8: 00 01 02
scene = selected target .sce
posinfo = tagged i16 x, tagged i16 y
exitid = selected exit id
```

Runtime on 2026-06-27 showed why this matters: returning `result=1` from
`16/2` made the client call `EnterSceneByMapName()` before the later `16/3`
confirm request, producing two visible loading sequences. The later
`16/3-scene-saved-target pending=1` trace still stuck on loading, so the
`16/2` fall-through scene-entry shortcut is treated as negative evidence, not a
valid protocol shortcut.

Re-check on 2026-06-27 identified the actual object-field getter used by
`mmGame:0x11CE`: object offset `0x4C` points to
`JianghuOL.CBE:0x01033C6C`, which returns `value[2]` for the matched field.
Therefore `result` must use the typed-u8 object encoding `00 01 xx`.
`raw-u32` (`00 00 00 xx`) and typed-u32 (`00 04 ...`) are both read back as
`0` by this getter. Returning a confirmation-only `16/2` object without
`scene/posinfo/exitid` will still crash if the field encoding is wrong, because
the branch falls through to `sub_BCC()` and tries to read missing `posinfo`.

Runtime crash evidence on 2026-06-27 then showed a second reason not to send a
bare result-only object. The response reached the client as:

```text
mock_teleport_stone_transfer subtype=2 ... response=16/2-confirm pending=0 confirm=1 resp=23
address unable access:4 ... pc=01033a42 lr=05183ba7
```

`JianghuOL.CBE:0x01033A42` is `stream_read_i16_be_tagged`; the crash state had a
null blob pointer. `mmGame:0x11CE` ignores `scene/posinfo/exitid` when
`result == 2`, but another synchronous reader can still require the target blob
shape during the same network callback. The server therefore returns
`16/2-confirm-target`: `result=2` plus the full target fields, while still
saving the target in the separate confirmation slot and not arming the actual
scene-change target until the follow-up `16/3` request.

An intermediate isolated-packet replay treated the follow-up `16/2` as a
standalone request and returned a zero-object WT acknowledgement. Full
`mmGame:sub_11CE(0x11CE)` evidence still proves `16/2 result=2` is the
recharge-prompt branch, but runtime later showed the real client batches
`16/2`, `16/3`, and optional item use into one WT packet. The combined handler
documented below supersedes the standalone interpretation for map confirmation.

### `1/16/3`

Parser evidence:

- `mmGameMstarWqvga.cbm:sub_11CE(0x000011CE)` subtype `3`
- requires `result == 2`, then calls `sub_BCC`.

Latest implementation for the teleport-stone path does not return a top-level
`1/16/3` object. It uses the confirmation target saved by `16/2`, records that
target for same-scene re-entry guarding, then answers the `16/3` request with a
single main-business scene-enter object:

```text
30/1 { scene=saved target .sce, posinfo=tagged i16 x/y }
```

Resource/task/actor-other data is intentionally left for the post-screen
follow-up request, matching the normal scene-change chain:

```text
30/1 -> same-screen ScreenInit -> 25/5+6/1+6/13+6/14+2/10 -> scene-task-subset
```

The runtime source name is still `builtin-teleport-stone-transfer`, but the
response kind is logged as:

```text
mock_teleport_stone_transfer subtype=3 ... response=scene-channel-enter-confirm-target ...
```

An empty `16/3` no-op avoided the crash but left the client on the loading
screen after:

```text
mock_teleport_stone_transfer subtype=3 ... response=16/3-noop pending=1 resp=11
```

The older two-scene-entry implementation needed to re-arm the scene-change
target serial before queueing the second response. Without that serial refresh,
the host screen manager suppressed the duplicate lifecycle request:

```text
screen_mgr same-suppressed caller=01018150 serial=1 scene=00_蓬莱仙岛01.sce pos=(223,382) exit=1
```

A later attempt to answer the `16/3` request with a `16/2` scene-entry object
did pass the screen guard, but it caused an infinite reload loop:

```text
mock_scene_target_remember serial=22 scene=00_蓬莱仙岛01.sce pos=(223,382) exit=1
mock_teleport_stone_transfer subtype=3 ... response=16/3-ack-via-16/2-scene pending=1 resp=82
screen_mgr same caller=01018150 ... serial=22 ...
```

The current implementation instead avoids creating a scene-change target during
`16/2`. It stores a separate teleport-stone confirmation target and logs:

```text
mock_teleport_stone_transfer subtype=2 ... response=16/2-confirm-target pending=0 confirm=1
mock_teleport_stone_transfer subtype=3 ... response=16/3-scene-confirm-target pending=0 confirm=1
```

Manual runtime on 2026-06-27 then reached the `16/3` response without crashing,
but no `mmGame` parser or scene follow-up log appeared after the normal queued
data event:

```text
mock_teleport_stone_transfer subtype=3 ... response=16/3-scene-confirm-target resp=81
net_send connect=2 wt=16/3 ... source=builtin-teleport-stone-transfer resp=81
net_queue_data connect=2 event=7 resp=81 ...
```

That older interpretation was incomplete. A later runtime with the direct
`30/1` response showed that reentrant flushing is also wrong for this phase:
it makes `ScreenInit` happen inside the `16/3` send call stack, and the fresh
screen can stall before emitting its post-enter follow-up. The `16/3` response
therefore stays on the normal async queue. The expected first diagnostic is:

```text
net_queue_data connect=2 event=7 resp=56 ...
```

The 2026-06-27 follow-up trace showed the event being consumed by the main
business callback, not by the mmGame transfer parser:

```text
net_fire ...
net_wrapper_state ...
net_wrapper_business ...
net_business_gate ...
net_done ...
```

IDA evidence in `JianghuOL.CBE`:

- `net_wrapper_event_dispatch(0x0103489A)` calls the net-manager callback
  first, then the main business callback at `0x01012E4D` when state is not `3`.
- `handle_version_update_response(0x01037473)` only scans kind `18`, so it
  ignores `16/3`.
- `net_business_response_dispatch(0x01012E4D)` parses the event packet, but its
  switch sends kind `16` to the default branch.
- `net_handle_scene_channel_dispatch(0x01039B8A)` handles kind `30`; subtype
  `1` calls `parse_scene_response_entry(0x010396D6)`, which reads `scene` and
  tagged `posinfo`.

Therefore a top-level `1/16/3` response can be syntactically valid and still
leave the client on the loading screen, because this callback path ignores
kind `16`. Returning `30/1` makes the response consumable by the observed
dispatcher while avoiding premature resource delivery into the old screen
instance.

After this direct scene-enter response, a narrow cleanup handler catches only
the pending teleport-stone flow's standalone `25/5` default-scene ack:

```text
mock_teleport_stone_direct_enter_ack scene=... pos=(...) keep_pending=1 response=25/5
```

This response does not clear the saved target. The saved target is cleared by
the later `scene-task-subset` completion path:

```text
mock_teleport_stone_direct_enter_followup scene=... pos=(...) response=scene-task-subset
```

If a duplicate `16/3` arrives after the real scene-enter response but before
the normal scene follow-up clears the pending target, it still returns
`16/3-duplicate-noop` and does not re-arm the scene target serial.

Negative evidence: `16/3` with `result` encoded as raw-u32 (`00 00 00 02`) is
read as `0` by `JianghuOL.CBE:0x01033C6C`, so `mmGame:0x11CE` skips
`sub_BCC()` and remains on the loading screen.

Do not re-parse `exitID` from the `16/3` request when a pending target exists.
The observed `16/3` request can contain bytes that make the generic field scan
mis-read an exit value such as `13633`; the correct exit id is the one selected
and saved during the `16/2` phase.

### `1/16/4`

Runtime request evidence from the automated teleport-stone map path:

```text
WT 00 25 01  01/16/4 len=33
payload:
  curid = 1
  objid = 22
```

Raw packet:

```text
5754002501100400210563757269640006000400000001056f626a69640006000400000016
```

IDA evidence:

- `Jianghu OL.CBE:SendItemUseReq(0x0103573A)` allocates
  `event(5, 1, 16, 4)`.
- It writes `curid` and `objid` from the teleport-stone map list item.
- 2026-06-30 IDA follow-up: `curid` is read from the selected current world-map
  row's teleport id, and `objid` is read from the target world-map row's
  teleport id. The selected child scene row is not serialized into the `16/4`
  packet. The client stores `wMap.lower_map + selected_child_offset` in local
  UI state before sending, but that is client memory and is not a valid runtime
  mock-server input. Treat it only as reverse-engineering evidence.
- In the first validated sample, `objid=22` matches the UI selection
  `HuoYanShan / YeHuoGu` and local SCE prefix `22HuoYanShan`.
- Earlier notes treated `curid` as a preferred exact scene index. That was
  wrong for city sub-map selection: a manual selection of `临安-西宣门` produced
  `curid=1 objid=4`, while local `sMap.dsh` shows `临安-西宣门` is row `49`
  (`c04临安府_03.sce @ 80,144`). From the server's visible `16/4` packet alone,
  this child row cannot be recovered.
- Runtime on 2026-06-30 produced `curid=1 objid=4` while the local
  `JHOnlineData` directory only contained Penglai SCE files. The old
  directory-scan mapping failed and logged `scene_source=default`, incorrectly
  entering `c00Penglai_01` instead of the selected LinAnFu map.
- Local `wMap.dsh` row `ID=4 / teleport_id=4` maps to `lower_map=47` and
  `scene_count=10`. Local `sMap.dsh` row `ID=47` contains the real target scene
  `c04LinAnFu_01.sce`. This pair is the authoritative local resource table for
  map-stone scene-name mapping.

Response strategy:

- return a same-subtype `1/16/4 {result:u8=0,value:u32=1}` object so the client
  enters its normal item-use confirmation callback;
- retain the resolved scene/position server-side across the callback's compact
  `16/2` and `16/3` requests;
- acknowledge the confirmed `16/2` with an empty WT packet, then return the
  validated `30/1 {scene,posinfo}` entry contract from `16/3`;
- do not use a fixed `(120,120)` landing point. The request carries only
  `curid/objid`, so the server maps those ids through the real local DSH tables:
  `wMap.dsh` chooses the sub-map row range and `sMap.dsh` supplies the target
  scene file name. The `sMap` position columns are world-map UI node positions,
  not actor coordinates (corrected by the 2026-07-16 evidence below).
- when the target SCE exists, choose an actual `edge_portal.spawn_point` and
  move it outside the trigger rectangle by the normal landing safety gap. If no
  edge portal exists, use the SCE dimensions only as a last-resort centre.
- when the target SCE is missing, keep the `sMap.dsh` scene file name in the
  response instead of falling back to the default scene, and preserve the
  `sMap.dsh` position fields while the client downloads the matching resources.
- exported `scene.json` files are reference material only and must not be used
  as a runtime data source.
- implemented mapping:

```text
objid=NN -> wMap.dsh row with teleport_id NN
visible 16/4 fields do not carry selected child sMap row
wMap.lower_map -> default sMap row
if wMap.scene_count > 1, mark row_source=wmap-base-ambiguous in trace
old fixed-coordinate behavior: curid=1 objid=22 -> 22HuoYanShan_01.sce @ (120,120)
old DSH-marker behavior: curid=1 objid=22 -> 22HuoYanShan_01.sce @ sMap.dsh UI position
current behavior: DSH resolves the scene; SCE edge_portal supplies the safe actor landing
current missing-resource behavior: DSH-derived scene name is preserved with scene download enabled
unresolved behavior: no row/scene/position fallback; log action=no-fallback and continue investigation
```

2026-06-30 missing-resource follow-up negative evidence:

```text
mock_teleport_stone_map_transfer curid=1 objid=4 scene=c04..._01 pos=(112,208)
mock_scene_target_remember serial=2 scene=c04..._01 pos=(152,208) exit=29793
```

Two separate findings were present:

- `curid=1` was incorrectly interpreted as child scene index `1`. Removing that
  interpretation still leaves an ambiguity: with only `curid=1 objid=4`, a
  real server cannot distinguish row `47` (`临安-南宣门`) from row `49`
  (`临安-西宣门`). The mock must not read local UI memory to fill this gap.
- the following `2/3` scene-change packet carried a non-authoritative `exitID`
  value (`29793` in the sample). Because pending-target inheritance only worked
  for `exitID=0`, the server fell through to SCE fallback and overwrote the DSH
  landing point with the scene center `(152,208)`.

Fix:

- `16/4` DSH mapping uses only server-visible packet fields and DSH resources.
  Multi-scene targets are explicitly traced as `row_source=wmap-base-ambiguous`
  until a packet-visible child-scene selector is identified.
- map-stone transfers set a separate `map_enter_pending` flag. The next
  same-scene `2/3` target parse inherits the saved DSH scene/position regardless
  of the later packet's `exitID`, and logs
  `mock_scene_target_inherit_map_transfer`.
- the target parse must not consume `map_enter_pending`. The dispatcher probes
  scene-change packets through several narrow detectors before calling the
  generic `builtin-scene-change` builder, so consuming the flag inside
  `vm_net_mock_get_scene_change_target()` can make the real builder lose the
  authoritative `16/4` scene/position.

2026-06-30 野猪林 follow-up negative evidence:

```text
mock_scene_target_inherit_completed scene=06..._01.sce pos=(96,120) exit=0
mock_scene_target_remember serial=15 scene=06..._01.sce pos=(96,120) exit=0
net_send wt=2/3 source=builtin-scene-change ...
mock_mmgame_scene_transfer_followup scene=06..._01.sce pos=(96,120) ...
mock_update_chunk file=m_mount.gif ...
ScreenInit Ok
mock_scene_target_inherit_completed scene=06..._01.sce pos=(96,120) exit=0
mock_scene_target_remember serial=16 scene=06..._01.sce pos=(96,120) exit=0
```

The first `25/5` correctly delivered the scene resource family plus `30/1`.
The client then downloaded a missing GIF and re-entered the same scene init.
That second `2/3` is a post-download repeat confirmation for the already
completed destination. It must not re-arm `g_vm_net_mock_last_scene_change_target`,
otherwise the next short `25/5` sends another resource family and the client
loops through update/init until it crashes.

Fix:

- completed-scene inheritance and completed-repeat suppression now share the
  same server-side reuse window;
- completed-repeat identity is scene plus landing coordinate, not `exitId`,
  because the repeat confirmation can legally arrive as `exitID=0` after a
  `16/4` destination was remembered with another entry id;
- repeated same-arrival `2/3` now logs
  `mock_scene_change_completed_repeat_ack` and remains an ack-only response.
  It must not be followed by another same-target
  `mock_mmgame_scene_transfer_followup`.

2026-06-30 雁门关 follow-up negative evidence:

```text
mock_scene_target_inherit_completed scene=c08..._01.sce pos=(80,160) exit=0
mock_scene_target_remember serial=26 scene=c08..._01.sce pos=(80,160) exit=0
net_send wt=2/3 source=builtin-scene-change ...
mock_mmgame_scene_transfer_followup scene=c08..._01.sce pos=(80,160) ...
```

This proved the previous fix was incomplete. The target parser inherited the
completed destination, but the inherited target could still carry an old
`needsSceneDownload` flag from the earlier missing-resource phase. In
`vm_net_mock_build_scene_change_combo_response()`, that download branch ran
before the completed-repeat rearm suppression and remembered the same target
again.

Fix:

- `vm_net_mock_mark_completed_scene_change_target()` clears
  `needsSceneDownload`; a completed enter target must be treated as past the
  download phase;
- completed-target inheritance also clears `needsSceneDownload`;
- combo handling logs `mock_scene_change_completed_stale_download` and clears
  the flag if a completed-repeat target ever arrives with stale download state.

2026-06-30 大理/金蛇谷 target negative evidence:

```text
manual target: 大理
runtime target: scene=19金蛇谷_01.sce pos=(80,168)
```

Local `wMap.dsh` shows the ambiguity:

```text
row ID=18 teleportID=19 name=金蛇谷 lower_map=126
row ID=19 teleportID=18 name=大理 lower_map=130
```

IDA evidence for `SendItemUseReq(0x0103573A)` says `objid` is the selected
world-map row's teleport id, not the DSH row id. The old server lookup accepted
`teleportId == objId || rowId == objId` in file order, so `objid=18` matched
row ID `18` first and incorrectly entered 金蛇谷.

Fix:

- wMap lookup uses only `teleportId == objId`;
- `rowId == objId` is not a valid server interpretation for `16/4`;
- if `teleportId`, `sMap` row, scene key, or landing position cannot be resolved
  from authoritative server data, the handler logs
  `mock_teleport_stone_map_unresolved ... action=no-fallback` and returns no
  synthetic target.

2026-06-30 大理 resource-completion negative evidence:

```text
mock_scene_target_inherit_map_transfer scene=c18DaLi_01.sce pos=(80,144)
mock_mmgame_scene_transfer_followup scene=c18DaLi_01.sce pos=(80,144) ...
mock_update_chunk file=c18DaLi_01.sce ...
ScreenInit Ok
mock_scene_change_completed_repeat_ack scene=c18DaLi_01.sce pos=(80,144) ...
mock_update_chunk file=18DaLi_01.map ...
pc=0x0100575e lr=0x0104678f
```

IDA/runtime interpretation:

- `0x0100575e` writes the decoded map-data pointer into the scene map-layer
  host object. The crash had a valid map-data pointer but an invalid host object
  (`r0=0x20000`), so the target mapping was no longer the primary failure.
- The server had marked the scene-change target as completed immediately after
  the first resource-family + `30/1` response, even though the client had only
  downloaded the SCE and still needed the SCE-declared primary MAP.
- The next repeat `2/3` was therefore treated as a completed repeat ack. When
  `18DaLi_01.map` arrived, the client rendered without a fresh pending scene
  enter and crashed in the map-layer bind path.

Corrected fix:

- a real server does not inspect the client's writable resource cache to decide
  whether a scene is ready;
- `handle_update_chunk_response(0x010372D6)` only consumes the `18/7` response,
  writes the chunk, clears network state, and returns. No separate
  server-visible "download completed" request was found after the final chunk;
- therefore the server-visible completion point is the final `18/7` response
  itself: `request.start + response.data_len >= response.totalsize`;
- when a final `18/7` resource chunk is delivered, the mock records
  `mock_update_chunk_complete ... action=allow-scene-reenter`;
- if the client then calls `EnterSceneByMapName(0x01018150)` for the same scene,
  the host same-screen guard allows that one re-enter and logs
  `screen_mgr allow-update-reenter`;
- this preserves the client-driven lifecycle after resource download without
  guessing a fallback scene, reading client cache state, or pushing an unsolicited
  business packet.

2026-06-30 follow-up negative evidence:

```text
mock_update_chunk file=c04LinAnFu_01.sce ...
mock_update_chunk file=04LinAnFu_01.map ...
net_send wt=6/1 source=builtin-scene-resource-followup ...
screen_mgr same ... scene= pos=(0,0) exit=0
```

The download itself succeeded, but the earlier `2/3` scene-change ack for a
missing SCE cleared `g_vm_net_mock_last_scene_change_target_valid`. The later
`25/5` and `6/1` follow-ups therefore lost the original `c04LinAnFu_01.sce`
target and fell back to current/default scene data. The fix is to keep missing
SCE targets pending across the download window:

```text
2/3 missing-SCE ack -> keep pending target
25/5 before file exists -> light ack only, keep pending target
18/7 chunks -> client writes SCE/MAP/GIF resources
6/1 after file exists -> refresh pending target from raw SCE, then emit resource follow-up + 30/1
later task subset -> close deferred target with normal 30/2/27 completion
```

2026-06-30 follow-up after the above fix:

```text
mock_scene_target_remember serial=2 scene=c04LinAnFu_01 pos=(0,0) exit=0
mock_mmgame_scene_transfer_followup scene=c04LinAnFu_01 pos=(0,0) ...
unhandled wt=2/10 len=84 objects=1 first=1/2/10:10,1/2/3:50,1/27/11:0,1/7/42:0
```

Two additional issues were identified:

- after the SCE had been downloaded, a later `2/3` request used `exitID=0` and
  did not repeat the map-stone coordinates. `vm_net_mock_get_scene_change_target()`
  now inherits a pending/recent completed target for the same scene before trying
  to re-derive an entry spawn. If no authoritative target exists, the server
  must not invent one for `16/4`; leave the packet unresolved and investigate
  the missing selector/data source.
- the client can emit a post-enter combo ordered as
  `2/10 + 2/3 + 27/11 + 7/42`. The existing post-enter handler only accepted
  `25/5 + 2/3 + 27/11 + 7/42`, so the new actor-other ordered variant is handled
  narrowly and reuses the same completion response family.

The older runtime directory scan has been removed from the `16/4` path. Map
stone targets must come from server-visible request fields plus `wMap.dsh` /
`sMap.dsh`; directory names are not an authoritative substitute for server map
data.

### 2026-07-11 桃花岛 `16/4` unresolved/assert

Runtime request: `1/16/4`, length `37`, fields `curid=1`, `objid=2`.
`SendItemUseReq(0x0103573A)` confirms those are the only two serialized map
selection fields. Local authoritative DSH rows resolve the request as:

```text
wMap teleportID=2 -> lower_map=40, scene_count=4
sMap row=40 -> 01桃花岛_01.sce @ (96,120)
```

The file and landing position were valid, but
`vm_net_mock_scene_name_is_download_key()` had been left as an unconditional
`false`. That rejected every DSH scene name before the normal resource check,
made `mock_teleport_stone_map_unresolved` return zero bytes, and exposed the
client's unhandled `16/4` assertion. The guard now accepts non-empty resource
keys without path separators; `vm_net_mock_scene_resource_exists()` remains the
subsequent authority for a real server resource.

### Post-enter Combo

Runtime negative evidence after `16/2`:

```text
unhandled wt=27/11 len=19 objects=1 first=1/27/11:0,1/12/1:0,1/7/42:0
```

This combo appears after `EnterSceneByMapName` has already started the same
screen scene lifecycle. It is now handled only while a teleport-stone target is
pending. The response mirrors the existing current-scene completion family and
does not append a second `30/2` scene-position commit:

```text
27/12 target scene + posinfo
27/11 empty NPC info
27/4 ready/finalize
7/42 empty books
```

## Server Behavior

Handled sources:

```text
builtin-teleport-stone-list
builtin-teleport-stone-transfer
builtin-teleport-stone-map-confirm
builtin-teleport-stone-post-enter
```

Autotest notes:

```text
mock_teleport_stone_list entries=1 exitinfo_len=...
mock_teleport_stone_transfer subtype=2 ... response=empty-wt-await-16/3 pending=0 confirm=1
mock_teleport_stone_transfer subtype=3 ... response=scene-channel-enter-confirm-target pending=0 confirm=1
mock_teleport_stone_direct_enter_followup scene=... pos=(...) objects=... response=scene-task-subset
mock_teleport_stone_map_confirm curid=... objid=... scene=... pos=(...) response=16/4-confirm value=1 scene_source=... pos_source=... download=...
mock_teleport_stone_post_enter scene=... pos=(...) objects=4
```

Direct-transfer and last-resort fallback target:

```text
scene = vm_net_mock_default_scene_name()
x = 223
y = 382
exitid = 1
```

Runtime override knobs are intentionally data overrides, not feature switches:

```text
CBE_TELEPORT_STONE_SCENE
CBE_TELEPORT_STONE_X
CBE_TELEPORT_STONE_Y
CBE_TELEPORT_STONE_LABEL
CBE_TELEPORT_STONE_EXIT_ID
```

## Automation Path

Validated route:

```text
enter map
key:3
key:3
tap a world-map destination
tap bottom-left confirm
tap a regional-map destination when needed
tap bottom-left instant-transfer
confirm if a prompt appears
```

The current deterministic smoke path uses the HuoYanShan world node and reaches
the selected `22HuoYanShan_01.sce` scene without unhandled packets. The handler
now uses the same `curid/objid` packet shape for any local map group that has a
matching SCE resource under `JHOnlineData`, and derives the landing coordinate
from the selected scene's SCE dimensions instead of a fixed point.

## Unknowns

- The third `exitinfo` per-row `u32` is stored by the client but has not been
  semantically named yet. It is currently emitted as `0`.
- Full UI validation should confirm whether the game sends `16/3` in other
  teleport-stone screen states. The validated path currently sends `16/2`
  followed by `27/11 + 12/1 + 7/42`.
- `curid` meaning is still partly unknown. It is useful as an exact-scene hint
  when a matching `[c]NN..._%02curid.sce` exists, but at least one validated
  request (`curid=20 objid=4`) requires falling back to the first local scene.
- The scan maps by local resource naming, not by an authoritative server table.
  If a live server later proves a different first landing scene for a specific
  `objid`, add a narrow override before the scan.

### 2026-06-30 Dali Child Scene Evidence

Runtime request:

```text
mock_teleport_stone_map_transfer curid=2 objid=18 smap_row=130 scene_count=3 row_source=wmap-base-ambiguous scene=c18大理_01 pos=(80,144)
```

Manual UI target was `大理校场`, but the server selected the wMap base sMap row.
Local server tables:

```text
wMap: teleportID=18 name=大理 lower_map=130 scene_count=3
sMap 130: c18大理_01.sce alias=大理街市 pos=(80,144)
sMap 131: c18大理_02.sce alias=大理校场 pos=(112,144)
sMap 132: c18大理_03.sce alias=大理东市 pos=(144,144)
```

New server rule:

- `objid` still selects the wMap row by `teleportID`;
- if `1 <= curid <= scene_count`, treat `curid` as the selected child index and
  use `lower_map + curid - 1`;
- otherwise keep the previous base-row behavior and mark `row_source=wmap-base-ambiguous`.

This keeps earlier `curid=20 objid=4` evidence from forcing an out-of-range child
row, while allowing visible child selections like `curid=2 objid=18` to resolve
to the authoritative `sMap.dsh` row.

### 2026-06-30 Dali Street Pending Consumption

Runtime negative evidence:

```text
mock_teleport_stone_map_transfer curid=1 objid=18 smap_row=130 scene_count=3 row_source=wmap-curid-index scene=c18大理_01 pos=(80,144) download=0
mock_scene_enter_defer phase=mmgame-scene-transfer-followup scene=c18大理_01 pos=(80,144) exit=1 missing=- keep_pending=1
mock_mmgame_scene_transfer_followup scene=c18大理_01 pos=(80,144) objects=9 resp=348 complete=0
```

Interpretation:

- `16/4` resolved correctly from server-visible DSH data: Dali street is
  `sMap` row `130`, scene `c18大理_01`, landing `(80,144)`.
- `missing=-` proves the stall was not a resource lookup failure.
- The failure was an event-order bug in the mock server: the target resolver
  consumed `g_vm_net_mock_teleport_stone_map_enter_pending` while narrow
  scene-change detectors were only probing the packet. When the real generic
  `2/3` response builder ran, it no longer inherited the saved `16/4` target and
  could mark the target as `needsSceneDownload`, causing the later `25/5`
  follow-up to stay `complete=0`.

Fix:

- `vm_net_mock_get_scene_change_target()` is now side-effect free for
  map-transfer inheritance;
- map-transfer pending is still cleared by the real completion paths
  (`post-enter`, `mmgame-scene-transfer-followup`, or task-subset completion),
  after the response contract has actually advanced.

### 2026-06-30 Server Resource Authority

Runtime negative evidence from a map-stone jump to the `23` area:

```text
mock_scene_target_inherit_map_transfer scene=23..._06.sce pos=(128,184) request_exit=0 saved_exit=1
mock_scene_enter_defer phase=mmgame-scene-transfer-followup scene=23..._06.sce pos=(128,184) exit=1 missing=- keep_pending=1
mock_mmgame_scene_transfer_followup scene=23..._06.sce pos=(128,184) objects=9 resp=353 complete=0
mock_update_chunk file=m_castle2.gif ...
screen_mgr allow-update-reenter scene=23..._06.sce pos=(128,184) exit=1 file=m_castle2.gif
```

Interpretation:

- the `16/4` DSH target was already correct: scene `23..._06.sce`, landing
  `(128,184)`;
- `missing=-` means the defer was not naming a concrete resource from a server
  missing-resource decision;
- `complete=0` came from a stale `needsSceneDownload` flag set while checking
  `JHOnlineData/<scene>` under the emulator working directory. When the emulator
  runs from `bin/`, that path is the client's writable cache, not the server's
  resource source.

Fix:

- scene existence and SCE parsing now read only server resource paths:
  `../web/fs/JHOnlineData` when launched from `bin/`, or
  `web/fs/JHOnlineData` when launched from the workspace root;
- named `18/7` update chunks use the same server resource source and no longer
  fall back to the client cache;
- if the client lacks a GIF/MAP/SCE locally, it still drives the normal `18/7`
  download flow. That download should not cause `16/4` targets to be marked as
  unresolved on the server side.

### 2026-07-16 Fresh Same-Target Map Transfer

Repeated map-stone operation reproduced a permanent loading progress bar:

```text
mock_teleport_stone_map_transfer curid=5 objid=1 ... scene=c00蓬莱仙岛_01
mock_scene_change_completed_repeat_ack scene=c00蓬莱仙岛_01 ... age=82
net_send ... wt=2/3 ... resp=96
mock_mmgame_scene_transfer_repeat_ack scene=c00蓬莱仙岛_01 ... age=87
net_send ... wt=25/5 ... resp=23
```

The completed-scene reuse test confused two different cycles:

- a post-download/repeated `2/3` confirmation has no new `16/4` provenance and
  must remain ack-only so it does not restart the scene;
- a new user-issued `16/4`, even when it resolves to the same scene and landing
  point inside the reuse window, has already opened a new loading cycle and
  still needs the normal resource-completion family.

`vm_net_mock_build_mmgame_scene_transfer_followup_response()` now applies the
ack-only repeat shortcut only when `map_enter_pending` is false. A fresh
same-target `16/4` continues through the existing resource objects plus
`30/2 result=2` without `posinfo`; it closes the loading cycle without issuing a
second coordinate-bearing scene entry.

### 2026-07-16 SCE-Space Safe Landing

The previous map-transfer path treated `sMap.dsh` columns `位置X/位置Y` as the
player's scene-space position. Cross-scene validation disproved that model:
many official rows point at isolated/blocked MAP cells, while the same SCE files
contain explicit edge-portal spawn points in scene coordinates. The DSH rows
also contain `位置X小/位置Y小` and directional neighbour IDs, confirming that
these coordinates describe the regional-map node layout.

Client movement evidence from `江湖OL.CBE`:

- `CheckMoveCollision(0x01045258)` probes the actor footprint and calls the two
  MAP collision helpers;
- `CheckMapMoveCollision(0x010451C2)` checks left/right boundary bits from the
  high nibble of adjacent MAP cells;
- `CheckMapMoveCollisionY2(0x0104512E)` checks top/bottom boundary bits.

Server landing rule:

1. `wMap.dsh` and `sMap.dsh` still resolve the requested scene key.
2. For an installed SCE, select the edge-portal spawn nearest the scene centre.
3. Apply `VM_NET_MOCK_SCENE_LANDING_SAFE_GAP` so the actor does not immediately
   retrigger that portal.
4. Use the SCE centre only when the scene has no edge portal. The DSH UI marker
   is retained solely for a missing-SCE download fallback.

Runtime replay of real `1/16/4` packets:

```text
Penglai TongQueTai: smap_marker=(96,112)  -> SCE entry 1 -> landing=(223,370)
TaohuaDao _03:      smap_marker=(128,152) -> SCE entry 0 -> landing=(66,313)
YeLianPo _01:       smap_marker=(96,120)  -> SCE entry 0 -> landing=(170,64)
HuoYanShan _01:     smap_marker=(80,152)  -> SCE entry 1 -> landing=(193,48)
```

The TongQueTai response `posinfo` bytes decode to `x=0x00DF (223)` and
`y=0x0172 (370)`, matching the service trace and replacing the old world-map UI
marker `(96,112)`.

### 2026-07-16 mmGame Direct-Entry Coordinate Unit Correction

An intermediate hypothesis encoded the direct-entry `16/2` coordinates as
`SCE pixel * 6`. Runtime retest still placed the actor in the blocked top-left
decoration, and the full client call chain disproved that conversion:

- `mmGameMstarWqvga.cbm:sub_BCC(0x0BCC)` reads both tagged signed-i16 values
  from `posinfo` and passes them unchanged to API-table offset `0x74`;
- `JianghuOL.CBE:stream_read_i16_be_tagged(0x01033A3A)` only decodes the
  tagged big-endian i16 and performs no coordinate scaling;
- `EnterSceneByMapName(0x0101809C)` stores the values unchanged at
  `R9+0x5C8E/+0x5C90`;
- `scene_runtime_init_and_sync(0x01012FB4)` copies those saved values unchanged
  into the active scene node at `+24/+26`;
- `scene_camera_follow_actor(0x01014C92)` consumes the node values directly
  against the SCE map dimensions.

For TongQueTai the map is `432x432`; the scaled `(1338,2220)` wire position is
therefore necessarily outside the map. The server now keeps and encodes the
authoritative safe SCE-pixel landing unchanged: `scene_pos=(223,370)`,
`wire_pos=(223,370)`, `coord_scale=1`. The SCE landing resolver and portal
safety gap remain in place; only the unsupported wire scaling was removed.

### 2026-07-17 Same-Scene Map Transfer Position Commit

The coordinate-unit correction was necessary but not sufficient. A full
automated `curid=1 objid=1` replay proved the old `16/2` response followed this
client state sequence:

```text
wire posinfo                       = (223,370)
EnterSceneByMapName arguments      = (223,370)
R9+0x5C8E/+0x5C90 saved position  = (223,370)
scene node +24/+26 after init      = (50,50)
```

`JianghuOL.CBE:scene_runtime_init_and_sync(0x01012FB4)` dispatches on
`sceneObj+0x1B4`. The mmGame `16/2 -> sub_BCC` path leaves this parser state at
zero. Case 0 rebuilds the active actor with the default `(50,50)` and does not
copy the saved target position.

The main-business scene entry parser
`JianghuOL.CBE:parse_scene_response_entry(0x010396D6)` supplies the missing
contract. Before calling `EnterSceneByMapName`, it writes parser state `7`.
`scene_runtime_init_and_sync` case 7 then copies `R9+0x5C8E/+0x5C90` to the
active node at `+24/+26`.

Consequently the final scene-enter stage must use a `30/1 {scene,posinfo}`
response rather than relying on the old `16/2 {result,scene,posinfo,exitid}`
direct-entry parser. The SCE landing resolver and unscaled tagged-i16 position
are unchanged; the fix is the client parser path that commits those coordinates
to the actor node. As documented below, this `30/1` now belongs to the confirmed
`16/3` stage rather than being returned prematurely to the initial `16/4`.

### 2026-07-17 World-Map Current-Node Refresh

Runtime after transferring from Penglai to `临安-南宣门` showed the scene and
actor position were correct, but reopening the world map still highlighted the
previous location. The request trace contained only:

```text
16/4 -> builtin-teleport-stone-map-transfer -> 30/1
```

There was no subsequent `16/2` or `16/3` request. Client evidence explains why
that leaves stale map state:

- `JianghuOL.CBE:0x0103573A` sends `16/4 {curid,objid}` and records the selected
  absolute `sMap` row;
- `0x010357E0` only dispatches the reply to `HandleItemUseConfirm(0x010190A8)`
  when the reply is still subtype `16/4`;
- `result=0` installs the normal confirmation callback;
- accepting it calls `ConsumeInventoryItem(0x01018F66)`, which sends `16/2`
  followed by `16/3` and performs the map controller's normal teardown;
- that teardown clears the cached map-sheet state, so the next world-map open
  reloads its current world/child indices from the newly selected `sMap` row.

The mock now returns `16/4 {result:u8=0,value:u32=1}` and stores the resolved
scene target server-side. After acceptance, the combined `16/2 + 16/3` request
reuses that target and returns the existing `30/1` scene-enter response; when a
stone is consumed, the same response places the existing item acknowledgements
before `30/1`. This preserves normal world-map refresh, inventory consumption,
and parser-state-7 position commit. No client memory or world-map globals are
written by the host.

Early isolated service replay validated the response fields and target mapping,
but did not reproduce the client's later multi-object batching:

```text
request  16/4 curid=1 objid=4
resolve  c04临安府_01 pos=(201,140) smap_row=47
response 16/4 result=0 value=1, 37 bytes
isolated request  16/2 exitID=47 type=3
isolated response zero-object WT acknowledgement, 5 bytes
real client       16/2 + 16/3 [+ 7/1] in one request; see the combo evidence below
```

### 2026-07-17 Teleport-Stone Cost

Runtime UI showed `您需要0个传送石` because the first map-confirm implementation
encoded `16/4.value=0` to make transfers free. Client evidence proves this field
is not reserved:

- `HandleItemUseConfirm(0x010190A8)` reads `value` into the saved one-byte item
  count and formats `您需要花费%d个传送石瞬移到%s` with it;
- accepting the dialog reaches `HandleItemUseDialogCb(0x01019068)`, which passes
  the same count to `ConsumeInventoryItem(0x01018F66)`;
- `ConsumeInventoryItem` operates on item ID `800`; when the client has no stone,
  its existing guard displays `传送石不够，是否购买？` before sending the scene
  exit requests.

The server now returns `value=1`, matching one teleport stone per map transfer.

### 2026-07-17 Confirmed Exit Is a Multi-Object Request

After changing the real cost from zero to one, runtime reached the confirmation
dialog but stalled after acceptance. The service trace ended at:

```text
mock_teleport_stone_transfer subtype=2 ... response=empty-wt-await-16/3
net_send ... wt=16/2 len=130 ... resp=5
```

No later request arrived. The `130`-byte boundary is the key negative evidence:
this is not a standalone short `16/2`. `ConsumeInventoryItem(0x01018F66)` calls
`SendSceneExitEvent(0x01018ED6)` twice and then removes item `800`; the outgoing
event layer batches those operations into one WT packet:

```text
1/16/2 {exitID,type}
1/16/3 {exitID,type}
1/7/1  {teleport-stone use fields}  # present when value/count is nonzero
```

The old first-object detector returned an empty response for `16/2`, thereby
discarding the already-present `16/3`. The new narrow combo handler requires the
saved `16/4` confirmation target, matching `16/2` and `16/3` exit/type fields,
and permits only the associated `7/1` item-use object. It extracts `7/1` through
the existing item-use builder, places its inventory acknowledgements first, and
appends the verified `30/1` scene-enter object last. Runtime source:
`builtin-teleport-stone-confirmed-exit-combo`.
