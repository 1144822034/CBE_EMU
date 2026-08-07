# 地图石进入蓬莱秘境后 NPC 未创建

## 结论

`c00蓬莱仙岛_03.sce`（蓬莱秘境）不是没有 NPC 目录。地图石的“目标资源已存在”路径在场景运行时表尚未可供 `27/11` 消费时，就把非空 NPC 目录放进当前场景完成的 `WT 2/3` 响应；该响应随后还以无坐标 `30/2` 关闭加载层。客户端后来发出的首个 `WT 6/1` 才处在可安全创建场景 NPC 节点的运行期，但一次性目录已经被前一个响应消耗，因此只会收到普通资源/任务后续包，画面没有 NPC。

修复应属于地图石直达场景的生命周期，而不能按蓬莱地图名称增加坐标、默认 NPC 或额外重试：当前场景完成的 `WT 2/3` 仍须回答一个空 `27/11` gate；紧随其后的真实 `WT 6/1` 再投递一次且仅一次非空 `27/11`。

## 证据记录

```text
phase: map-stone direct-enter NPC seed
status: implemented; runtime validation pending

request:
  wt_kind: 2 then 6
  wt_subtype: 3 current-scene completion, then 1 scene-resource follow-up
  objects: 2/3 requests 27/12 + 27/11 + 27/4 + 7/42; follow-up is client scene-runtime WT6/1
  key_fields: direct-map-stone target c00蓬莱仙岛_03.sce, exit=39, pos=(157,47)
  sample_len: current-completion response 547 bytes in bin/server_out.txt
  packet_log: mock_teleport_stone_confirmed_exit_combo -> mock_teleport_stone_deferred_enter -> mock_teleport_stone_current_scene_complete

response:
  wt_kind: 2 then 6
  wt_subtype: 3 then 1
  objects: early 27/11 gate, later one 27/11 NPC directory
  fields: npcnum:u8, npcinfo:raw
  arrays: four selected dynamic NPC rows in the captured destination catalog
  strings: scene-owned actor and script resource fields from the selected dynamic rows
  blobs: npcinfo rows consumed by scene_parse_npcinfo_and_spawn_npcs

ida_evidence:
  binary: 江湖OL.CBE (IDA instance selected by binary_name)
  function: scene_runtime_init_and_sync(0x01012FB4), scene_parse_npcinfo_and_spawn_npcs(0x01037998), scene_actor_asset_slot_table_load_entry(0x01044E48)
  dispatch_case: 27/11
  parser_reads: npcnum/npcinfo; each row id/x/y/four strings/final actor id, then creates type-21 scene node
  failure_branch: early entity rows precede the scene runtime/actor-asset table lifecycle; the later safe runtime request has an already-consumed one-shot seed

runtime_evidence:
  trace_lines:
    - mock_teleport_stone_confirmed_exit_resolve ... final_scene=c00蓬莱仙岛_03.sce pos=(157,47)
    - mock_scene_npc_catalog scene=c00蓬莱仙岛_03.sce source=service-dynamic delivery=initial actors=4 selected=4 rows=4 dynamic=4 npcinfo_len=302
    - mock_scene_npc_seed phase=current-scene-completion scene=c00蓬莱仙岛_03.sce ... npcnum=4 ... once=1
    - mock_teleport_stone_current_scene_complete ... response=27-family+30/2-no-posinfo
    - scene_npc_request_snapshot scene=c00蓬莱仙岛_03.sce selected=4 total=4 dynamic=4
  handled_source: builtin-scene-change, followed by normal scene-resource/task requests
  queued_event: event 7
  client_effect: destination map completes, while the four catalogued NPC nodes are absent

negative_evidence:
  missing_or_bad_field: none; actor rows and npcinfo are non-empty and selected correctly
  observed_failure: only the direct map-stone no-download completion consumes the catalog before the first WT6/1; the existing copper-terrace-specific defer already documents the safe follow-up phase

unknowns:
  - name: exact visual contents of the four dynamic rows
    current_value: unchanged; use the existing catalog builder unchanged
    why_kept: the failure occurs after selection, so changing dynamic NPC data would hide the lifecycle violation rather than fix it
```

## Expected packet ownership

1. The deferred map-stone event sends the normal destination `30/1` scene enter.
2. Its no-download current-scene `WT 2/3` completion responds with `27/12`, **empty** `27/11`, `27/4`, `7/42`, and final no-posinfo `30/2`. This preserves the requested FB gate and lets `scene_handle_change_result_scene_pos(0x01039770)` reset download state without a second coordinate-bearing entry.
3. The client then drives the already-created scene shell into `scene_runtime_init_and_sync(0x01012FB4)` and sends `WT 6/1`.
4. That first real follow-up consumes the pending catalog through `27/11`; `scene_parse_npcinfo_and_spawn_npcs(0x01037998)` creates type-21 nodes and `scene_actor_asset_slot_table_load_entry(0x01044E48)` resolves their actor visuals.

The transition marker is the existing pending-but-not-seeded catalog for the same recently completed scene, not a hard-coded `_03` name. Normal arrivals already consume that marker in their completion response, so they do not enter this branch. Shop-return reseeding is separately session-scoped and remains excluded.

## Planned change and checks

- Generalize the existing direct-map-stone/Tongquetai deferred `27/11` branch to every `closeTeleportDirectEnter` completion.
- Generalize the paired first-`WT 6/1` delivery condition from the Tongquetai scene name to the same pending, unseeded, recently completed current scene.
- Keep the current selected NPC rows, response object order, scene position, task refresh behavior and one-shot protection unchanged.
- `make -j2` completed successfully after the change.

## Runtime validation boundary

The live map-stone path still needs one client run. It is successful only if the server log has, in order:

1. `mock_scene_npc_seed_defer scene=c00蓬莱仙岛_03.sce ... reason=direct-map-stone-shell-not-runtime-ready`;
2. exactly one `mock_scene_npc_seed_deliver scene=c00蓬莱仙岛_03.sce phase=WT6/1 after=direct-map-stone-current-scene-completion` and its paired `mock_scene_npc_seed ... npcnum=4`;
3. no second position-bearing scene entry, repeated loading overlay, or repeated NPC seed.

The visual assertion is that all four existing catalogued NPCs appear immediately after the destination scene becomes interactive.
