# Dynamic NPC scene round-trip lifecycle

## Evidence record

```text
phase: return to a previously visited scene containing service-dynamic NPCs
status: implemented; isolated service regression validated

request:
  wt_kind: 2/3 followed by 25/5
  objects: scene-change maptype/mapID/exitID, then the scene task/resource subset
  key_fields: mapID is the destination scene; exitID is resolved through the source SCE portal
  sample_len: 75 then 44 in the captured client run
  packet_log: builtin-scene-change, builtin-mmgame-scene-transfer-followup

response:
  wt_kind: 27/11 plus resource/task objects and 30/2 completion
  objects: 27/11 npcnum+npcinfo before task rows, then 30/2(no-posinfo)
  key_fields: npcnum is nonzero for c00蓬莱仙岛_03 when its dynamic NPC row is enabled
  sample_len: determined by current MySQL NPC/task catalog
  client_dispatch: scene_parse_npcinfo_and_spawn_npcs(0x01037998); parse_scene_posinfo_field(0x01039770)
  next_request: normal 25/5 and scene-sync polling; no delayed NPC fallback is required

runtime_evidence:
  trace_lines: before the fix scene_ready appeared for c00蓬莱仙岛_03 without a matching mock_scene_npc_seed; after the fix mock_scene_npc_rearm and mock_scene_npc_seed appear in the same completion response
  handled_source: builtin-scene-change or builtin-mmgame-scene-transfer-followup
  queued_event: event 7
  client_effect: returning to the scene recreates the dynamic NPC immediately

negative_evidence:
  missing_or_bad_field: the one-shot NPC state was only rearmed by 30/1
  observed_failure: both local edge-portal completion paths intentionally use 30/2 because the client has already created the destination scene shell; the old session-wide seeded flag therefore suppressed the new shell's 27/11 catalog

unknowns:
  - name: scenes with no NPC rows
    current_value: no nonempty 27/11 is appended; the next real scene boundary rearms the target independently
    why_kept: an empty catalog is not needed to create nodes, while repeated empty responses add no client-visible value
```

## Client contract

The IDA instance was selected by `binary_name=江湖OL.CBE`, not by a fixed
instance id. `scene_parse_npcinfo_and_spawn_npcs(0x01037998)` allocates a new
104-byte record array from `npcnum`, reads each `npcinfo` row, and calls
`scene_node_find_or_create` for every NPC. This is creation data for the current
scene shell, not persistent cross-scene state.

`parse_scene_posinfo_field(0x01039770)` is the separate 30/2 completion path.
Using 30/2 does not implicitly replay the 27/11 NPC directory. Therefore every
fresh local scene shell that completes without 30/1 must explicitly rearm and
consume the NPC one-shot.

## Root cause and fix

Two established local-portal paths intentionally avoid a second 30/1 because it
would reopen the loading screen:

- the full `2/3` scene bootstrap ending in a position-bearing 30/2;
- the deferred `25/5` mmGame follow-up ending in 30/2 without posinfo.

Both now call `vm_net_mock_mark_scene_moveinfo_npc_seed_pending(target.scene)`.
When target resources are ready and the target has NPC rows, they append the
nonempty 27/11 catalog before resource/task objects and the final 30/2. The
ordering lets task prompt refresh operate on nodes created for the new shell.
Repeat acknowledgements remain unchanged and do not replay the catalog.

## Regression

`tmp/scene-npc-roundtrip-regression.php` logs in, leaves
`c00蓬莱仙岛_03`, returns to it, and accepts the catalog from whichever of the
two valid completion phases owns the fresh shell. It asserts that 27/11 is
nonempty and precedes 30/2 when both are present in the same response.

