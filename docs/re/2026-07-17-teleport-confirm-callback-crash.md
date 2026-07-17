# Teleport Confirmation Callback Lifetime Crash

```text
phase: teleport-stone confirmed exit -> scene entry
status: implemented

request:
  wt_kind: 16
  wt_subtype: 2 (first object)
  objects: 16/2 + 16/3 + optional 7/1
  key_fields: matching exitID/type; 7/1 consumes item 800
  sample_len: 130
  packet_log: tmp/mock-service-19090.20260717-190640.stdout.log

response:
  current_event: item acknowledgement only (or empty WT when 7/1 is absent)
  deferred_event: 30/1 {scene,posinfo}
  delivery: account-local scene-sync poll, not the request callback
  minimum_boundary: a scheduler tick after the confirmed-exit request

ida_evidence:
  binary: JianghuOL.CBE
  function: EnterSceneByMapName(0x0101809C)
  dispatch_case: different scene class calls screen-manager remove at 0x01018136
  parser_reads: 30/1 reaches the normal scene-enter path
  failure_branch: 0x01018136 -> loading_gif_widget_draw_frame return 0x01046189 -> DrawVerticalTileStrip store 0x01005AF4

runtime_evidence:
  trace_lines: screen_mgr remove exposes new_top=0x0105A814, then dp_change from the scene screen to that window
  handled_source: builtin-teleport-stone-confirmed-exit-combo
  queued_event: service scene-sync poll
  client_effect: confirmation callback finishes before the scene-entry object is parsed

negative_evidence:
  missing_or_bad_field: none; response phase was wrong
  observed_failure: DrawVerticalTileStrip receives *a1 == 0 and attempts STRH at null+0x24

unknowns:
  - name: exact original-server delay
    current_value: next 100 ms scheduler tick or later
    why_kept: client lifetime evidence requires a separate callback; no trace proves a longer delay
```

## Runtime boundary

The last pre-fix service trace shows that item removal and scene entry were
combined in one response:

```text
mock_item_use item=800 ... resp=125
mock_scene_target_remember ... scene=06..._01.sce pos=(170,64)
mock_teleport_stone_transfer subtype=3 ... resp=54
mock_teleport_stone_confirmed_exit_combo ... item_response=125 scene_response=54 resp=174
net_send ... wt=16/2 len=130 ... resp=174
```

The crash immediately after the screen removal was:

```text
screen_mgr remove ... new_top=0105a814
dp_change ... caller=01018136
address unable access:24 ... r2=0 r3=24 lr=01046189 pc=01005af4
```

At `DrawVerticalTileStrip(0x01005AC4)`, `0x01005AF4` is the halfword store
through the strip pixel pointer. The saved first argument points at global
`0x01056B2C`, whose first word is zero in this crash. The caller is the frame
draw of the global scene-loading GIF widget at `0x01056CC4`.

The newly exposed screen table at `0x0105A814` contains the CBM/payment window
callbacks (`0x01044A47`, `0x01044A55`, `0x01044AA5`, `0x01044AA9`). Therefore
the server did not send malformed coordinates: it entered the next scene while
the teleport confirmation/consume-item callback still owned a transient CBM
screen. Removing the current scene exposed that stale window and its destroyed
image owner.

## Implemented contract

1. Parse and consume the real combined `16/2 + 16/3 [+ 7/1]` request.
2. Return only the inventory acknowledgement in its synchronous response.
3. Store the resolved scene target in the current account state.
4. On a later scene-sync poll and scheduler tick, emit one `30/1` response.
5. Mark the scene pending and save the target position only when step 4 emits.

This keeps all progress packet-driven and removes the lifetime violation
without patching client code or forcing CBE globals.
