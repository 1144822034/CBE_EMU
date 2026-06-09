# Session Log

Add short dated notes here when a change or discovery is important but not yet worth a larger write-up.

Suggested entry format:

```text
## YYYY-MM-DD
- changed:
- verified:
- evidence:
- next:
```

## 2026-06-07
- changed: removed the active runtime skip/suppress guards from `src/main.c` that changed guest parser/draw/state-machine control flow to force scene progress. This includes the portal move-entry append skip, scene loading/strip suppressions, selector null-table/draw bypasses, portrait null-callback bypass, and scene-bootstrap loading-overlay suppression. Trace-only hooks remain allowed.
- changed: added a project rule to `AGENTS.md`: do not add runtime guards, hook branches, or emulator shims that skip the game's own parser, scene, draw, or state-machine logic to force progress; fix the upstream platform/server/resource contract instead.
- evidence: these guards were active hook branches that changed return values, guest memory, table counts, or PC (`vm_bx`) rather than merely recording evidence. The most recent portal guard experiment also showed why this is unsafe: skipping the descriptor callback removed the top-center map/status text while failing to resolve the visible fragments.
- next: treat the portal-tail mismatch as confirmed evidence, but continue by identifying the correct resource/scene callback contract or server response that lets the game parse valid actor/world state itself.

## 2026-06-07
- corrected: the first descriptor-boundary guard at `0x0100DB32` was too early. The next visual rerun showed the protagonist fragments still visible and the top-center map/status text missing, even though the log confirmed `trace_actor_motion_portal_callback_skipped` and no later body-entry append. This disproves the callback-skip as a final fix.
- changed: replaced that callback-skip with a narrower append-count guard at `sub_100F094()` `0x0100F468`. The parser is allowed to keep running, but if the candidate 32-byte move entry matches the confirmed portal-derived shape (`actorId=1`, `gridX=223`, `box=203,402,240,422`, `kind=2`), the emulator logs `trace_portal_move_entry_append_skipped`, skips only `entryCount++`, and resumes at `0x0100F46A`.
- evidence: IDA disassembly shows `0x0100F448..0x0100F468` writes the candidate `ActorMoveEntryEx` fields and then increments the table count at `R9+0x5D40`; skipping only the final count write prevents `scene_draw_actor_pass()` from consuming the bogus portal record while preserving later `sub_100F094()` side effects such as map/status text parsing.
- next: rebuild and rerun manually. Expected logs are `trace_actor_motion_callback_handoff` followed by `trace_portal_move_entry_append_skipped`, no `trace_scene_body_draw_dispatch` for `box=203,402,240,422`, and top-center map/status text restored. If fragments remain, add a separate image-blit owner trace for the visible fragment region.

## 2026-06-07
- verified: the descriptor-boundary guard fired on the latest manual rerun. `trace_actor_motion_portal_callback_skipped` appears at `pc=0x0100DB32`, `tick=862`, with `cb=0x0100F095` and tail shorts `3,2,223,382,8,1,5,1`.
- evidence: after that skip marker in `bin/logs/net_trace.log`, the latest session no longer logs `trace_actor_move_entry_append` or `trace_scene_body_draw_dispatch` for the portal-derived `actorId=1` / `box=203,402,240,422` entry. The current-node path stays alive: repeated `trace_scene_current_node_draw ... actorId=10001 ... visualRes=054045a8 grid=223,382 screen=103,65` follows the skip, and `trace_scene_visual_res_probe reason=ready` remains stable.
- packet status: the latest `bin/logs/net_packets.log` session reaches `source=builtin-scene-resource-followup` and has no new `unhandled_packet` / `assert(0)` after that point. Older log lines still contain a prior-session `WT len=9 hdr=7/18` unhandled marker; do not attribute that stale marker to the latest post-guard run unless it reappears after a fresh session start.
- next: ask the operator to confirm the visual result. If the extra protagonist fragments are gone while the protagonist remains visible, the portal-callback guard is confirmed visually. If any sprite body is now missing, the next fix should synthesize or locate a real `sub_100F094()` actor-record stream instead of broadening the guard.

## 2026-06-07
- changed: added a narrow descriptor-boundary guard for the current protagonist sprite-fragment issue. At `parse_actor_motion_descriptor()` callsite `0x0100DB32`, if the pending callback is `sub_100F094+1` and the descriptor tail is the confirmed `c00蓬莱仙岛_01.sce` portal grammar prefix `3,2,223,382,8,1,5,1`, the emulator logs `trace_actor_motion_portal_callback_skipped` and takes the function's natural callback-null branch at `0x0100DB3A`.
- evidence: IDA `sub_100F094()` expects an actor-record stream and appends body/world entries only for actor tags `2`/`9`; `tools/inspect_sce.py bin/JHOnlineData/c00蓬莱仙岛_01.sce` shows offset `135` is an edge portal with spawn `(223,382)` and trigger rect `(203,402,240,422)`. The latest runtime log shows exactly those portal words handed to `sub_100F094`, followed by `trace_actor_move_entry_append ... actorId=1 ... box=203,402,240,422`, and the user screenshot confirms those erroneous protagonist sprite pieces are visible beside the now-visible main actor.
- implication: this is not a draw-function patch and does not force CBE actor globals. It prevents the confirmed local resource/descriptor grammar collision from creating a bogus body/world move-entry while preserving the validated `actorinfo` and compact `1/2/10 otherinfo` mock formats.
- next: rebuild, rerun manually, and check for `trace_actor_motion_portal_callback_skipped`; the expected visual result is that the current-node protagonist remains visible while the extra protagonist fragments disappear. If the main actor disappears too, the next hypothesis is that this same portal-derived body entry was still carrying the only body draw path and needs replacement with a correctly encoded actor-record stream rather than suppression.

## 2026-06-07
- verified: the coordinate-alignment experiment changed the live protagonist draw state in the intended direction. On the next manual rerun, `trace_scene_current_node_draw label=current_node_drawAt` reports `grid=223,382` and `screen=103,65` for `actorId=10001`, replacing the earlier `grid=12,10` / `screen=0,-67` shape. `visualRes` remains ready and status divide sites stay healthy.
- evidence: latest `bin/logs/net_trace.log` shows `trace_scene_actorinfo_snapshot ... actorId=10001 ... preview=h_warriorwalk1.gif actor=h_warrior.actor scene=c00蓬莱仙岛_01`, `trace_scene_visual_res_probe ... reason=ready`, and repeated `trace_scene_current_node_draw ... actorId=10001 ... grid=223,382 ... screen=103,65`. `bin/logs/stdout_trace.log` has no crash, and `bin/logs/net_packets.log` reaches the accepted `source=builtin-scene-resource-followup`.
- implication: the visible protagonist problem is now much less likely to be an actorinfo field-order mismatch. The narrow server-side coordinate seed fix is confirmed useful. The persistent `trace_scene_body_draw_dispatch` entry is still the portal-derived `actorId=1` / `box=203,402,240,422` record, but it is no longer the only path expected to place the protagonist on-screen.
- next: ask the operator whether the protagonist is now visibly correct. If not, the next evidence gap is visual quality/animation layer selection around `drawAt=0x01045579` and the still-unexplained body/world portal branch, not basic actor resource binding or spawn coordinate seeding.

## 2026-06-07
- changed: adjusted the mock server's default scene/player coordinate seed from the older guessed `(120,120)` / actor grid `(12,10)` mix to the active scene resource's first entry spawn `(223,382)`. The new helper defaults `scene posinfo`, `0x1B/12.posinfo`, actorinfo trailing grid/motion words, and compact `1/2/10 otherinfo` grid words to the same coordinate, while keeping `CBE_SCENE_POS_X/Y`, `CBE_ACTOR_GRID_X/Y`, and `CBE_ACTOR_MOTION_ARG0/1` overrides.
- evidence: IDA `parse_scene_response_entry()` (`0x010396D6`) reads `posinfo` as two tagged i16 values and forwards them into the scene object method `+116`; `ui_apply_named_posinfo_target()` (`0x0100E9B8`) uses the same grammar for the `0x1B/12` target/location path. `tools/inspect_sce.py` parses `bin/JHOnlineData/c00蓬莱仙岛_01.sce` first edge portal as `spawn_point=(223,382)` and `trigger_rect=(203,402,240,422)`.
- implication: this is a narrow server-response hypothesis, not a draw patch. If the next run changes `trace_scene_current_node_draw ... grid=12,10 screen=0,-67` to a scene-coordinate grid and visible screen position, the remaining protagonist display issue is primarily coordinate seeding rather than actorinfo field order. If it does not, revert or override the coordinate experiment and continue tracing draw-time world-to-screen state.
- next: rebuild, then rerun manually and inspect `trace_scene_actorinfo_snapshot`, `trace_scene_current_node_draw`, and body/world entry traces.

## 2026-06-07
- verified: IDA parser-vs-builder audit did not find a confirmed server-field order mismatch in the currently relevant mock builders. `vm_net_mock_build_actor_info()` still matches the fresh-enter `parse_actorinfo_response()` read order for the visible self-actor fields, and `vm_net_mock_append_actor_other_empty10_object()` matches `sub_1012958()` as a compact scene-node tuple rather than a `sub_100F094()` record stream.
- evidence: IDA `parse_actorinfo_response()` (`0x0100FA88..0x0100FD22`), `net_handle_actor_move_info()` case `10` (`0x01012DD6`), and `sub_1012958()` (`0x01012958`) cross-checked against `src/main.c` builders. Runtime `bin/logs/net_trace.log` still shows the bad body entry being produced by `parse_actor_motion_descriptor()` handing `.sce` portal tail shorts `3,2,223,382,8,1,5,1` to `sub_100F094()`, before the later `scene-resource-followup` response is processed.
- implication: do not rewrite `1/2/10 otherinfo` or expand `actorinfo` again to chase the bad body/world sprite. The current confirmed mismatch is local semantic pairing: actorinfo's valid trailing scene key reaches `scene_rebuild_runtime_nodes()`, which passes `{ sceneKey, mode=10, callback=sub_100F094+1 }`, so `c00蓬莱仙岛_01.sce` portal geometry becomes a bogus `actorId=1` move-entry.
- next: identify why the first fresh-enter `scene_rebuild_runtime_nodes()` path chooses the non-null `sub_100F094` callback for a scene-key resource, or find an upstream policy/descriptor source that should make that initial mode-10 call callback-null until prompt-local data exists.

## 2026-06-07
- changed: added a narrow built-in response for the post-scene interaction composite request `WT len=61 hdr=2/10 objs=1/2/10,1/14/14,1/14/4,1/14/5,1/14/6`. The new `vm_net_mock_build_scene_interaction_followup_response()` replies only with the confirmed `1/2/10 otherinfo` compact self-node tuple and leaves the `14/*` request family unechoed.
- evidence: IDA `net_business_response_dispatch()` (`0x01012E4C`) has no `kind=14` response case, while `kind=2 subtype=10` is consumed by `net_handle_actor_move_info()` case `10`; returning `14/*` objects would be skipped by the main business dispatcher on current evidence.
- verified: first manual rerun after the new `len=61` mock did not re-trigger that packet family. `net_packets.log` ended at the accepted `WT len=49` `source=builtin-scene-resource-followup`, and `net_trace.log` continued through scene `draw_pass/status_panels/actor_pass` ticks with no `unhandled_packet`, no `assert(0)`, and no `source=builtin-scene-interaction-followup`.
- evidence: `bin/logs/net_packets.log` latest session has requests through tick `132` only, ending with `WT len=49 hdr=12/1 objs=1/12/1,1/7/42,1/6/1,1/6/13,1/6/14,1/2/10,1/25/5`; grep across `bin/logs/*.log` found no `WT len=61`, `builtin-scene-interaction-followup`, `unhandled`, or `assert` markers.
- next: rerun with a more deliberate in-scene click/tap path that previously emitted `WT len=61`; if the mock fires, confirm the client accepts the `1/2/10`-only response, otherwise preserve any new outbound WT payload and object summary.

## 2026-06-07
- verified: second manual rerun still did not re-trigger the post-scene `WT len=61` interaction family. The new session reached the same accepted `WT len=49` scene-resource follow-up, this time at tick `143`, then stayed in local scene `draw_pass/status_panels/actor_pass` ticks.
- evidence: `bin/logs/net_packets.log` now contains two sessions; the newest ends with `source=builtin-scene-resource-followup responseLen=319 WT len=49 hdr=12/1 objs=1/12/1,1/7/42,1/6/1,1/6/13,1/6/14,1/2/10,1/25/5`. A grep across `bin/logs/*.log` still finds no `WT len=61`, no `send_payload len=61`, no `builtin-scene-interaction-followup`, no `unhandled`, and no `assert` marker.
- next: the current live blocker is not exposing a new wire packet on this path. Keep the `len=61` mock in place for the known prior interaction path, but the next high-value work is to either reproduce the exact click path that emits it or pivot back to the local scene draw/move-entry state visible in `trace_scene_body_draw_dispatch`.

## 2026-06-06
- changed: added a new narrow built-in mock for the first post-HUD resource-miss composite request in `src/main.c`. It matches only the current `WT len=49` packet carrying `1/12/1, 1/7/42, 1/6/1, 1/6/13, 1/6/14, 1/2/10, 1/25/5` plus `Type=101`, and replies with the currently safest statically-supported subset: `12/1(learnednum=0, learnedskill=\"\")`, `7/42(booknum=0, booksinfo=\"\")`, `6/1(taskinfo empty blob)`, `2/10(othernum=0)`, and `25/12(result=4)` via the existing info-banner clear shell. `make` passes on this build.
- changed: kept `6/13` and `6/14` deliberately absent from that new reply for now. Static dispatch now shows `6/13` consumes a fixed six-entry `tasktypes` blob and `6/14` first branches on field `action`, so fabricating empty objects there is more likely to trip a deeper parser than to unblock scene safely.
- evidence: `src/main.c` `vm_net_mock_is_scene_resource_followup_request()` and `vm_net_mock_build_scene_resource_followup_response()`; static IDA on `net_handle_task_response_dispatch()` (`0x0104726C`), `net_handle_actor_move_info()` case `10` (`0x01012DD6 -> sub_1012958()` reading `othernum/otherinfo`), and `net_handle_info_banner_state()` (`0x01010C7E`, only confirmed clear path is subtype `12 result=4`).
- next: rerun once on the rebuilt binary and check whether `logs/net_packets.log` now shows `source=builtin-scene-resource-followup` instead of `assert(0)`, then use `logs/net_trace.log` to see whether the client accepts this partial follow-up shell or immediately exposes the next exact missing object family.

## 2026-06-06
- changed: added two narrow read-only resource traces in `src/main.c`: `trace_resource_request_enqueue` / `trace_resource_request_mark` on the net-manager missing-resource queue path (`0x010365F0` / `0x010366AC`), and widened `trace_update_request_prepare` so `send_update_chunk_request()` now logs `reqType=1/2/4` with best-effort GBK names. `make` passes on this build.
- verified: the first-scene HUD divide-by-zero is no longer the active blocker on the newest clean session. `net_trace.log` now shows `trace_status_meter_seed_fallback` once at `tick=198` on the confirmed empty-head path (`n2=1 sourceHead=00000000`), and the later divide-site watcher reaches both bars with nonzero denominators: `primary ... displayMax=120` and `secondary ... displayMax=100`.
- verified: the next live blocker is later and different. After `scene_runtime_tick label=actor_pass` at `tick=200`, the client immediately hits a local resource-open miss for GBK name `侠剑江湖`, then sends a new unhandled `WT len=49` composite request with object sequence `1/12/1, 1/7/42, 1/6/1, 1/6/13, 1/6/14, 1/2/10, 1/25/5`. This is the first post-HUD blocker on the current branch.
- evidence: `bin/logs/net_trace.log` lines `2933` (`trace_status_meter_seed_fallback`), `3204..3208` (`draw_pass -> status_panels -> actor_pass`), `3210` (`trace_resource_open_helper ... namePtr=0540010a`), and `3214..3215` (`unhandled_packet WT len=49 ...`); `bin/logs/net_packets.log` tail for the same `len=49` dump; `bin/logs/storage_trace.log` `file_open_fail path=JHOnlineData/��������` with `file_open_fail_mem_r5_0540010a` bytes `cf c0 bd a3 bd ad ba fe`, which decode to `侠剑江湖`; newest `bin/logs/stdout_trace.log` session starts but does not append a new `Arithmetic exception: Divide By Zero`.
- next: rerun once on the rebuilt binary and inspect the new queue traces around the same `tick~200` window. The immediate split is whether this `len=49` packet is the net-manager's missing-resource request for `侠剑江湖`, or whether a different scene/bootstrap path is constructing it after the local-open miss.

## 2026-06-04
- verified: the new `ownerObj.asyncCounter/asyncPending` watcher rules out the “pending never clears” theory. Before the first `99/1`, `sub_564_popup_tick` increments `ownerObj.asyncCounter` once per tick (`0 -> 31`), then at `tick=110` it resets the counter back to `0`, reaches `sub_564_before_cb24_dispatch`, and `sub_564_after_cb24_dispatch` shows a successful `cb24`/`open_channel` cycle (`R0=1`) that immediately writes `ownerObj.asyncPending = 1`.
- verified: that pending state is only temporary. After the `99/1` reply is consumed and the stale `&unk_C20` queue item is committed (`queueText=050177f0`, `queueActive=1`), runtime later clears `ownerObj.asyncPending` back to `0` at `tick=124` (`last=05017612`), so the bridge is not blocked by a permanently nonzero pending field.
- verified: the real stop happens one step earlier. Once the stale queue is live, later title-loop windows no longer show `sub_564_popup_tick`, `sub_564_before_cb24_dispatch`, or `sub_564_after_cb24_dispatch` at all; they only show `sub_59E_popup_dispatch` plus `shared_popup_bridge_cb30_guard`. Correspondingly, `ownerObj.asyncCounter` stays pinned at `0` for the rest of the delayed run while `queueActive=1` remains set.
- implication: the current missing contract is no longer “clear `ownerObj.asyncPending`” and no longer “rearm `eventObj+16` directly”. More narrowly, the stale `&unk_C20` queue state prevents the title loop from re-entering `sub_564()` itself, so the `cb24/open_channel/event=5 -> alloc_outgoing_game_event()` chain never restarts.
- evidence: `bin/logs/net_trace.log` `tick=80..110` for `ownerObj.asyncCounter` growth and `tick=110` `sub_564_before/after_cb24_dispatch`, `tick=118` `sub_1032_queue_commit`, `tick=124` `ownerObj.asyncPending -> 0`, and later `tick~763+` windows where `sub_59E_popup_dispatch` continues with `queueActive=1` but `asyncCounter=0` and only `cb30_guard` remains.
- next: stop prioritizing bridge-pending semantics. The next narrow static/runtime target is `sub_59E` itself: identify the local gate that makes `queueActive=1 / queueText=&unk_C20` stop calling `sub_564`, because that is now the most direct reason no further `event=5` producer cycle can occur.

## 2026-06-04
- verified: the active title popup driver `mmTitleMstarWqvga.cbm::sub_564()` is now statically matched to main-CBE `startup_maybe_start_async_net_task()` (`0x0103A77C`). Both functions gate on a local `+0x24` pending field and `+0x20` counter, call shared-bridge `cb28`, then call the same scene-object method at `*(R9+21676)+0x15C`, and finally feed the return value into shared-bridge `cb24`.
- verified: that shared scene-object method is `sub_1018DC6()`, not an event producer itself. Static `scene_object_vtable_init()` sets object slot `+0x15C` to `sub_1018DC6`, and `sub_1018DC6()` just returns the host string from `mmorpg_config` or the fallback `jhol.51coolbar.com:20888`.
- implication: the real title-to-main bridge is now narrower than “some code should call `alloc_outgoing_game_event()`”. The periodic chain is `sub_564/sub_103A77C -> sub_1018DC6(host lookup) -> cb24/open_channel -> event=5 callback -> alloc_outgoing_game_event()`. So the next runtime question is whether `99/1` leaves `ownerObj+0x24` nonzero or otherwise stops the next `cb28/cb24/open_channel` cycle before it can queue another `event=5`.
- changed: extended the read-only title popup watcher to log `ownerObj+0x20/+0x24` (`asyncCounter/asyncPending`) and added internal `sub_564` trace points at relocated `0x05017154/0x05017168/0x0501716A` so the next rerun can show host lookup, `cb24_dispatch`, and its return state directly on the stuck title path.
- evidence: static decompile of `mmTitleMstarWqvga.cbm::sub_564()` and `0x0103A77C`, plus main-CBE `scene_object_vtable_init()` slot recovery showing `a1+0x15C = sub_1018DC6`; latest `bin/logs/net_trace.log` `tick=100..107` already shows the runtime half of the same chain (`cb28_poll -> cb24_dispatch -> open_channel connect=2 -> event=5 -> event_packet_add_field`).
- next: rerun once on the rebuilt binary and compare `sub_564_before_cb24_dispatch` / `sub_564_after_cb24_dispatch` before and after the stale `&unk_C20` queue commit. The narrow split is now: did `ownerObj.asyncPending` stay nonzero, or did the bridge stop reaching `cb24_dispatch` entirely after `99/1`.

## 2026-06-04
- verified: on the active stuck title path, the observed rearm caller is `alloc_outgoing_game_event()` (`0x0100E2E4`), not any of the title-local queue helpers. The new `trace_event_packet_builder` shows both live `event_packet_add_field()` hits using the same LR `0100E2F9`, which IDA resolves to `alloc_outgoing_game_event`.
- verified: this caller does not run automatically after the stale `&unk_C20` queue commit. In the latest run, the first rearm is the original pre-queue cycle at `tick=107`, then the queue commits `queueText=050177f0` at `tick=108`, and no second `event_packet_add_field()` appears until `tick=331`, when a fresh local touch/submit path runs. Between those points, `sub_59E_popup_dispatch` and `cb30_guard` keep looping with `queueActive=1`, but `eventField10` stays `0`.
- verified: the first obvious static candidate family `sub_100E342()` is not a title-local popup helper but a main-CBE scene/event state machine. Its callers are `sub_100E976()` and large scene-side controller `sub_1016D2E()`, and both eventually lead into scene/runtime state around `R9+24884` rather than the current title module locals. So it is useful as an `alloc_outgoing_game_event()` example, but not yet evidence that the stuck title queue was supposed to pass through `sub_100E342()` itself.
- implication: the missing contract has narrowed again. The question is no longer "who calls `event_packet_add_field()` in general" but "what path was supposed to call `alloc_outgoing_game_event()` again after `&unk_C20` became active". Current evidence says the popup queue/bridge path itself never does.
- evidence: `bin/logs/net_trace.log` lines with `trace_event_packet_builder label=event_packet_add_field ... lr=0100e2f9` at `tick=107` and again at `tick=331`, the intervening `sub_1032_queue_commit` at `tick=108`, and the long `cb30_guard`-only span with `eventField10=0`.
- next: stop over-focusing on `sub_100E342()` as the direct missing title callback. The better next target is whichever title-to-main bridge is supposed to enter an `alloc_outgoing_game_event()`-producing main-CBE path after `&unk_C20` queue activation, because the current stale queue loop never reaches any such caller.

## 2026-06-04
- verified: the shared child event object behind `objC+32` is an outbound WT builder, not an inbound parser. Static `sub_103478E()` calls `eventObj+40` (`event_packet_calc_size`) and `eventObj+32` (`event_packet_build_WT`) before sending bytes through the net-manager bridge, then resets the same object with `event_packet_init(..., 0, n10, n19)`. So the earlier `eventObj+16` rearm question is specifically “who adds the next outbound packet object”, not “who reparses the next inbound WT”.
- verified: current title-local queue helpers do not answer that question directly. `sub_10C6()` is only `sub_1032(..., a3=0)`, and `title_activate_popup_screen_if_idle()` only activates `v2+10700`, optionally runs `queueCallback`, and clears `queueActive`; neither helper touches the shared child event object or its `event_packet_*` method table.
- changed: added read-only caller tracing for `event_packet_add_field()` (`0x010345D4`) and `event_packet_init()` (`0x010346E0`), plus direct write tracing for the active shared event-builder fields `+5/+16/+20/+24/+28/+32/+36/+40`. Build validation is pending the next `make`.
- evidence: static decompile/disassembly of `sub_103478E()`, `event_packet_build_WT()`, `event_packet_calc_size()`, `sub_10C6()`, and `title_activate_popup_screen_if_idle()`; instrumentation now lives in `src/main.c`, `src/hookRam.c`, and `src/main.h` under `trace_event_packet_builder` / `trace_event_packet_write`.
- next: rerun once on the rebuilt binary and capture the first `trace_event_packet_builder label=event_packet_add_field` after the stale `&unk_C20` queue commit. The narrow target is now the direct LR/caller that repopulates, or fails to repopulate, the outbound builder after the first pump/reset cycle.

## 2026-06-04
- verified: the delayed-main-login experiment is a partial positive result. The delayed `1/1/x` success packet is definitely still pending late into the run (`delay_login_success_event ... delayTicks=360`, then `net_task_slots ... slot0[e=7 d=151 r1=00000168 ...]` at `tick=495`), so the later title-local behavior is not being driven by accepted login success yet.
- verified: under that delay, the earlier "stuck forever at `25`" model is no longer true. Right after `99/1`, the usual generic cleanup still happens and `obj8_stateA` remains pinned at `25` for a while with `obj8.flag10=0`; but later in the same delayed run the local title flow re-enables the widget on its own, and `obj8_stateA` climbs again until at least `209` by `tick=494` while `byte2` is still `0` and the delayed login-success `event=7` is still waiting in the scheduler.
- implication: this strongly supports the `sub_5B0` timeout-path hypothesis. The `99/1` generic cleanup does not permanently kill the role-manage timer; it only interrupts it temporarily. The active experiment simply ended before the local counter crossed the built-in `>300` threshold, so we still have not observed whether `title_activate_role_manage_screen` will fire first when enough ticks are allowed to pass.
- evidence: `bin/logs/net_trace.log` `delay_login_success_event ... account=123 delayTicks=360`, `queue_data_event_delayed ... label=login-success-main`, tail `net_task_slots label=dispatch_end tick=495 slot0[e=7 d=150 r1=00000168 ...]`, and `trace_title_flow_action label=sub_5B0_check_byte2 ... obj8_stateA=209 ... byte2=0` at `tick=494`.
- next: rerun the same delayed binary again but leave it on the active `010534b4` title screen longer, until either `obj8_stateA` crosses `300` and `title_activate_role_manage_screen` appears, or the delayed success event finally fires first. No code change is needed for that narrower verification.

## 2026-06-04
- changed: built a minimal timeout-validation binary for the `99/1 -> role-manage` hypothesis. The scheduler now supports per-event delay, and the default build delays only the non-`1234` main login-success `event=7` (`request=1/1/1`) by `360` ticks. The `99/1` exchange, the `1234 -> result=2` failure mock, and all later scene/game packet families remain unchanged. Build validation succeeded (`make` passed).
- evidence: `src/main.c` now logs `delay_login_success_event ... experiment=title-role-manage-timeout` plus `queue_data_event_delayed ... label=login-success-main delayTicks=360` for the delayed main login-success object.
- next: rerun once on this rebuilt binary and inspect whether the active `010534b4` path now keeps ticking long enough for `obj8_stateA` to grow past the `>300` threshold in `sub_5B0`, or whether it still freezes at `25` immediately after the `99/1` generic cleanup even with later login success held back.

## 2026-06-04
- verified: `sub_722()` is the local disarm/reset path that the current `99/1` flow never reaches. Static decompile shows it clears `*(R9+10312)+16 = 1`, resets `10332/10336/10340/10344` to `0`, and zeroes the same child widget counter field at `*(_WORD *)(*(_DWORD *)(R9+10308) + 10)`. Its named callers are `sub_74A()`, `role_list_screen_handle_action()`, and `role_manage_screen_handle_role_list_nav()`, so this is the existing role-list / role-manage-side return path rather than a generic scene-enter path.
- verified: the current stuck title screen already contains a timeout path toward role-manage, but the `99/1` generic cleanup appears to stall it too early. In `sub_5B0`, when `obj2C == 1`, the branch at `0x700..0x71C` checks `*(_WORD *)(*(_DWORD *)(v0 + 8) + 10) > 300`; if true, it clears local child flags and queues `title_activate_role_manage_screen` via `sub_10C6(&unk_82C, title_activate_role_manage_screen)`. That same `+10` field is the traced `obj8_stateA` counter.
- implication: the current `99/1` fallback-to-generic-cleanup now has a plausible concrete failure mode. Because runtime `0501760c` clears `titleLoadingGate`, `loading_gif_widget_draw()` drops `obj8.flag10` to `0`, and `obj8_stateA` freezes at `25` instead of continuing toward the `>300` threshold that would queue `title_activate_role_manage_screen`. Later login success then arrives first and sets `byte2=3`, so `sub_5B0` takes the direct `sub_EC(3)` teardown path instead of the dormant role-manage timeout path.
- evidence: static decompile of `sub_722()`, `title_activate_role_manage_screen()`, `sub_5B0()`, `sub_74A()`, and `role_manage_screen_handle_role_list_nav()`; paired runtime evidence where `obj8_stateA` stops at `25` immediately after the `0501760c` gate clear and never approaches the `>300` threshold before login success lands.
- next: the most useful next experiment is no longer “who clears the gate”. The narrower question is whether the real `99/1` contract is supposed to keep that counter running long enough to reach the built-in `>300 -> title_activate_role_manage_screen` branch, or whether some other packet/local callback is supposed to call `sub_722()` / `title_activate_role_manage_screen()` directly before account-login success is accepted.

## 2026-06-04
- verified: runtime `0501760c` is not an external helper; it is the tail of `mmTitleMstarWqvga.cbm::sub_938(event=7)` itself (`sub_938+0x104`, static offset `0xA3C`). Static decompile shows that after the packet scan finishes, case `7` unconditionally executes:
  - `*(_DWORD *)(*(_DWORD *)(R9+10312) + 12) = 0`
  - `*(_BYTE  *)(*(_DWORD *)(R9+10312) + 16) = 0`
  This exactly matches the runtime `objC.flag10` / `titleLoadingGate` clear at `01056100`.
- verified: the earlier login-success clear at runtime `050175cc` is the same field family, but from the accepted-object branch inside `sub_938`. Static offset `0x9FC` clears `*(_BYTE *)(v12 + 16) = 0` immediately after `sub_938` accepts `type=1/subtype=2|3|6` and before it checks the router gate. So both the ignored `99/1` path and the accepted login-success path clear the same local gate byte; the difference is that only the accepted branch also sets `R9+10302` (`ownerObj.byte2`) to `3` or `4`.
- implication: the current `99/1` echo does not have its own special consumer in `sub_938(event=7)`. Because case `7` only recognizes packet objects with `type=1` and `subtype=2|3|6`, a pure `99/1` data event simply falls through the scan loop and reaches the generic tail cleanup at `0xA36..0xA3C`. That explains the exact observed behavior: `titleLoadingGate` is cleared, the loading GIF stops, but `obj2C/local10344` remain armed and `byte2` stays `0`.
- evidence: IDA decompile/disassembly of `sub_938` (`0x938..0xA4E`), especially the accepted-branch writes at `0x9FA/0x9FC` and the generic case-7 tail at `0xA36/0xA3C`; paired runtime writes in `bin/logs/net_trace.log` at `tick=108 last=0501760c`, `tick=157 last=050175cc`, and `tick=157 last=050175b8`.
- next: the next useful static target is no longer “which helper is `0501760c`”, because that is resolved. The narrower remaining question is what packet or local path is supposed to keep `sub_938(event=7)` out of that generic tail cleanup after `99/1`, or alternatively what other consumer outside `sub_938` is expected to handle the `99/1` semantic and clear `obj2C/local10344` before later login success sets `byte2=3`.

## 2026-06-04
- verified: the corrected owner/gate watcher run finally pins the meaningful `99/1` side effect to the title loading gate, not to `obj8_stateA`. On the active `010534b4` path, `trace_title_child_state_write` shows `ownerObj=0105340c` remaining armed (`obj28=1 obj2c=1 local10340=1 local10344=1`) after `sub_938_case5_arm_mode1`, while the later `99/1` `event=7` callback clears `objC.flag10` at `01056100` with `last=0501760c`; the paired global watcher shows the same byte is `titleLoadingGate` (`R9+21808`).
- verified: that gate clear immediately explains the observed GIF freeze. Right after the `0501760c` clear, `loading_gif_widget_draw()` drops `obj8.flag10` from `1` to `0` at `last=010461c8`, and the local frame counter (`obj8_stateA`) stays pinned at `25`. This confirms `obj8_stateA=25` is only a loading-widget frame value and that the real `99/1` semantic effect in the current build is "disable the loading gate/widget", not "advance a business stage".
- verified: the same run also confirms what `99/1` does **not** do. Even with the gate cleared and the extra queued `event=9` close already firing, the owner object never disarms before login success arrives: `obj28=1 obj2c=1 local10340=1 local10344=1 byte2=0` remain stable through the whole `tick~107..114` window. Later login success still sets `ownerObj.byte2=3` at `last=050175b8`, after which `sub_5B0` removes `010534b4` and routes into `sub_EC(3)`.
- evidence: `bin/logs/net_trace.log` around `tick=107..108` and `tick=156..157`, especially `trace_title_child_state_write label=ownerObj.obj2C ... new=1`, `trace_title_child_state_write label=objC.flag10 ... new=0 ... last=0501760c`, `trace_business_callback_write label=titleLoadingGate ... new=0 ... last=0501760c`, `trace_title_child_state_write label=obj8.flag10 ... new=0 ... last=010461c8`, and the later `trace_title_child_state_write label=ownerObj.byte2 ... new=3 ... last=050175b8`.
- next: stop using `obj8_stateA=25` as the semantic target. The narrowest remaining question is what static helper contains runtime `0501760c`, why it clears `titleLoadingGate` on the current `99/1` path, and what additional condition or packet should also clear `obj2c/local10344` before login success is allowed to set `byte2=3`.

## 2026-06-04
- changed: corrected the child-state watcher anchor from `base+10308` to the real `sub_5B0` owner object at `base+10300` (`0105340c` on the active run). The same rebuild also adds `R9+21808` to the global callback/gate write watcher as `titleLoadingGate`, so the next rerun can identify who actually clears the GIF-widget enable gate around the `99/1` window. Build validation succeeded (`make` passed).
- next: rerun once on the rebuilt binary and inspect `trace_title_child_state_write label=ownerObj.*` plus `trace_business_callback_write label=titleLoadingGate` around `tick~107..114`. The immediate question is whether `99/1` or some later local/business callback writes `R9+21808` to `0`, and whether the real owner `byte2/obj2C` fields also change in that same window before login success arrives.

## 2026-06-04
- verified: the new child-state run resolves `obj8_stateA`. `obj8_cb1c=010461a9` maps to `loading_gif_widget_draw()`, and its callee `01046048 loading_gif_widget_draw_frame()` increments `a1[5]` (`obj8+0x0A`, the traced `stateA`) by `1` on every draw tick while `obj8.flag10` is enabled. So the observed `obj8_stateA=0..25` sequence is a local loading/animation frame counter, not a login business stage value.
- verified: the same static/runtime pair also explains why `stateA` freezes at `25` after the current `99/1` exchange. `loading_gif_widget_draw()` only resets `obj8.stateA` to `0` when it sees the controlling global byte at `R9+21808` transition into the enabled state; when that gate is cleared, it just drops `obj8.flag10` back to `0`. In the latest rerun, `obj8_stateA` climbs from `20` to `24` before `sub_938_case5_arm_mode1`, reaches `25` at the `tick=107` `sub_5B0_check_byte2` sample, then remains pinned while `obj8.flag10` falls to `0` after the `99/1` data-event window.
- corrected: the first `trace_title_child_state_write` implementation is watching `base+10308`, which is the child widget pointer (`obj8=01056cc4`) rather than the `sub_5B0` owner object at `0105340c`. That is why the repeated `label=localObj.byte2` lines are actually overlapping writes inside the GIF widget (`last=0104609e` from `loading_gif_widget_draw_frame()`), not real writes to the screen-local `byte2`.
- evidence: `bin/logs/net_trace.log` around `tick=93..108`, especially `obj8_cb1c=010461a9`, `obj8_stateA=20..25`, the `last=0104609e` write traces, and the post-`99/1` `obj8_flag10 -> 0` freeze with no `stateA` reset; plus static decompile of `010461A8/01046048`.
- next: stop treating `obj8_stateA=25` as the missing semantic itself. The narrower remaining question is which code clears the controlling gate behind `loading_gif_widget_draw()` during or after `99/1`, and whether the real title-side contract should instead re-enable a different local mode before login success sets `byte2=3`. If another run is needed, fix `trace_title_child_state_write` to anchor on the live `sub_5B0` owner (`R4`) rather than `base+10308`.

## 2026-06-04
- changed: added a narrower child-state watcher for the active `010534b4` title flow. `src/main.c` now logs `trace_title_child_state_write` for writes touching the current local object (`byte2/obj28/obj2C/obj30`), `obj4.state8/flagD`, `obj8.stateA/flag10`, `objC.flag10`, and module locals `10340/10344`. `trace_title_flow_action` now also dumps the live method slots for `obj4`, `obj8`, and `objC`, so the next rerun can map child-state changes back to concrete code owners instead of only observing their post-tick values. Build validation succeeded (`make` passed).
- next: rerun once on the rebuilt binary and inspect the `99/1` window for `trace_title_child_state_write` around `tick~107..114`. The immediate question is whether `obj8.stateA=25` is written by the `99/1` data-event path itself, by the local `obj8` update method that `sub_5B0` calls every tick, or by a later shared-owner callback that only becomes visible after the packet is drained.

## 2026-06-04
- verified: the default-on `0x63/1 -> event=9` transport experiment is a confirmed negative result. In the latest rerun, `short_63_close_event_enabled request=0x63/1 closeAfterData=1` is followed by the expected queued close (`queue_close_event connect=2 ... event=9`), and that close really fires later at `tick=114`. But the local title object never disarms: after the `99/1` data event, `obj28=1 obj2c=1 local10340=1 local10344=1` remain set, `obj8_stateA` stays pinned at `25`, `byte2` remains `0`, and repeated `sub_5B0_mode1_branch / sub_5B0_check_byte2` traces continue unchanged until later login success sets `byte2=3`.
- verified: this means the missing `99/1/0` contract is not explained by transport close sequence alone. Adding `event=9` after the current `builtin-short-63-1-echo` does not reproduce any visible `sub_722()`-style disarm/reset or any other local title-state transition before account-login success arrives.
- evidence: `bin/logs/net_trace.log` around `tick=107..114`, especially `short_63_close_event_enabled`, `queue_close_event connect=2 ... event=9`, `fire_event slot=1 event=9`, and the unchanged `trace_title_flow_action` snapshots showing persistent `obj2c=1 local10344=1 byte2=0 obj8_stateA=25`; later `tick=510` still reaches `sub_EC(3)` only after login success makes `byte2=3`.
- next: stop treating missing close/event ordering as the leading explanation for `99/1`. The next narrow target should shift back to the response semantics around the armed mode-1 local object itself: either a different `0x63/1` payload shape, or an additional business packet/state change that specifically moves `obj8_stateA` off `25` or clears `obj2c/local10344` before login success.

## 2026-06-04
- changed: rebased the `0x63/1 -> event=9` transport-side control into the default build. The current `bin/main.exe` now enables the short-control close event by default after the existing `builtin-short-63-1-echo` payload, while still allowing `CBE_SHORT_63_CLOSE_EVENT=0` to disable it for regression comparison. Build validation succeeded (`make` passed).
- next: rerun once on the default binary and compare the `sub_938_case5_arm_mode1` window against the prior no-close baseline. The immediate question is whether the added `event=9` is enough to disarm or advance the mode-1 `sub_5B0` object before later login success sets `byte2=3`.

## 2026-06-04
- changed: added a transport-side control for the narrowed `0x63/1` question. `src/main.c` now supports `CBE_SHORT_63_CLOSE_EVENT=1`, which leaves the existing `builtin-short-63-1-echo` payload unchanged but appends the normal queued `event=9` close after the `event=7` data callback for this one short-control exchange. Default behavior remains unchanged when the env var is unset. Build validation succeeded (`make` passed).
- next: rerun the current title path once with `CBE_SHORT_63_CLOSE_EVENT=1` and compare the same `sub_938_case5_arm_mode1` window. The concrete question is whether an added `event=9` close is enough to disarm or advance the mode-1 `sub_5B0` object, or whether the real missing contract still has to be in some other packet/transport side effect.

## 2026-06-04
- verified: the new `sub_938_case5_arm_mode1` trace confirms the full early title pre-arm sequence on a clean rerun. Before `tick=109`, repeated `trace_title_flow_action label=sub_5B0_load_mode` lines still show `obj28=0 obj2c=0 local10340=0 local10344=0`. At `fire_event ... event=5`, `sub_938_case5_arm_mode1` fires immediately, and in the very next `sub_5B0_load_mode` sample the same object flips to `obj28=1 obj2c=1 local10340=1 local10344=1`.
- verified: the current built-in `99/1` reply is now a stronger negative control. After `send_call connect=2 len=9`, the emulator returns `mock_short_wt_control_echo kind=99 subtype=1 len=9` / `source=builtin-short-63-1-echo`, but the following `event=7` callback leaves all visible title state unchanged (`trace_title_login_state` pre/post identical) and does not disarm the local title object. `sub_5B0` remains in mode-1 with `byte2=0` and continues to loop there until later login success sets `byte2=3`.
- evidence: `bin/logs/net_trace.log` around `tick=104..112` and `tick=343..344`, especially `trace_title_router_gate label=sub_938_case5_arm_mode1`, the immediate `obj28=1 obj2c=1` transition, the `mock_short_wt_control_echo` / `builtin-short-63-1-echo` block, and the unchanged `trace_title_login_state` plus persistent `trace_title_flow_action label=sub_5B0_mode1_branch` / `sub_5B0_check_byte2 byte2=0` lines.
- next: treat the current `99/1` echo as insufficient rather than merely “unrelated”. The next narrow experiment should focus on recovering the real semantic contract for this title-side `99/1/0` exchange, because the current stub demonstrably leaves the direct-enter preconditions armed in place.

## 2026-06-04
- verified: the upstream arming write is now pinned to `sub_938(event=5)` itself. Static `mmTitleMstarWqvga.cbm::sub_938()` case `5` writes `*(R9+0x283C+0x28)=1` and `*(R9+0x283C+0x2C)=1`, which are the same live `obj28=1` / `obj2C=1` values later seen in `trace_title_flow_action`. The same case immediately sends `net_build_login_request(99, 1, 0)`.
- verified: the latest runtime log matches that contract. On `activeScreen=010534b4`, a second `open_channel connect=2` at `tick=110` queues `event=5`; when it fires at `tick=117`, the client sends a `len=9` `99/1` request and the emulator answers it with `source=builtin-short-63-1-echo`. The later `event=7` data callback for that `99/1` response leaves the visible title state unchanged, but by the time normal login success arrives the local `sub_5B0` object is already in the armed `obj28=1 / obj2C=1` mode-1 state.
- evidence: `mmTitleMstarWqvga.cbm` disassembly/decompile for `sub_938()` case `5` (`0x0958..0x0962`), plus `bin/logs/net_trace.log` around `tick=110..118` (`open_channel connect=2`, `queue_event ... event=5`, `fire_event ... event=5`, `send_call connect=2 len=9`, `mock_short_wt_control_echo kind=99 subtype=1 len=9`) and `bin/logs/net_packets.log` `tick=109 source=builtin-short-63-1-echo`.
- next: the next narrow contract question is no longer "who armed `obj2C`", but what real server/business meaning `99/1/0` carries on this title screen and whether its response is supposed to keep mode `1`, clear it, or route into a different local state before account login success sets `byte2=3`.

## 2026-06-04
- verified: the new `trace_title_flow_action` identifies the immediate title-side teardown condition. On the latest isolated `1/1/3` success, the local `sub_5B0` object at `0105340c` enters `obj2C=1` / mode-1 state long before the login reply completes. Right after `sub_938` accepts the `1/1/3` object, the same local object flips `byte2` from `0` to `3`, and `sub_5B0` immediately takes the `n2==1` branch: `sub_5B0_check_byte2` sees `byte2=3`, `sub_5B0_remove_current_screen` fires at `050172c6`, then `sub_5B0_call_sub_EC` calls `sub_EC(3)`, after which `01053450` is created.
- verified: this means the current direct-enter path is no longer best explained by a missing network-side title follow-up after `sub_938`. The decisive branch is already armed in local title state before the reply lands: `obj28=1`, `obj2C=1`, `local10340=1`, and `local10344=1`. The login success then only supplies the final trigger by setting `byte2=3`, which the existing local `sub_5B0` mode-1 logic interprets as “remove current screen and route by `sub_EC(3)`”.
- evidence: `bin/logs/net_trace.log` around ticks `153..162`, especially `trace_title_flow_action label=sub_5B0_load_mode ... obj28=1 obj2c=1`, `trace_title_flow_action label=sub_5B0_check_byte2 ... byte2=3`, `trace_title_flow_action label=sub_5B0_remove_current_screen pc=050172c6`, and `trace_title_flow_action label=sub_5B0_call_sub_EC ... r0=00000003`; paired `trace_title_router_gate` lines still show `routerFlag17=1 routerFlag18=1` and no `sub_938_call_router_callback`.
- next: the narrow remaining question is now upstream of the final login success object: what earlier local title path or startup-mode contract pre-arms `obj2C=1` / `local10344=1` on `0105340c`, and should that path instead leave `obj2C=0` (or a different `byte2`) until a real role/manage workflow has been entered.

## 2026-06-04
- verified: the first successful `trace_title_router_gate` run disproves the earlier “router gate byte missing” suspicion. On the latest isolated `1/1/3` success, `sub_938_set_login_state` / `sub_938_check_router_flags` show `local10340=1`, `local10344=1`, `routerFlag17=1`, and `routerFlag18=1` before `010534b4` is torn down. There is no `sub_938_call_router_callback` trace in that same window, so the direct-enter path is **not** being triggered by `router+44`; the router gate is already in the suppress-callback state.
- verified: the actual screen teardown happens later in the local title mini-flow, after `post_data_event` and just before `screen_manager idx=6 remove r0=010534b4`, with `last=050172c6` inside `sub_5B0`. This narrows the remaining branch further: the decisive title-stage condition now appears to live in the local `sub_5B0` object state (`v0+2`, `v0+0x2C`, related child flags), not in the earlier `sub_938` router gate.
- changed: added a second read-only watcher `trace_title_flow_action` for the `sub_5B0` local mini-flow. `src/main.c` now logs the object pointer at `R4`, `byte2`, `obj+0x28/+0x2C/+0x30`, child object flags, and the `sub_EC()` parameter path around `sub_5B0_load_mode`, `sub_5B0_mode1_branch`, `sub_5B0_check_byte2`, `sub_5B0_remove_current_screen`, `sub_5B0_load_sub_EC_arg`, `sub_5B0_call_sub_EC`, and `sub_5B0_clear_byte2`. Build validation succeeded (`make` passed).
- next: rerun once and inspect `trace_title_flow_action` in the same `1/1/3` or `1/1/2` success window. The concrete target now is to identify which local object state (`byte2`, `obj2C`, or child flags) is already primed when `sub_5B0` reaches `050172c6`, because that is the condition currently collapsing the expected title-stage flow into immediate screen removal.

## 2026-06-04
- changed: added a new read-only `trace_title_router_gate` watcher around the narrowest remaining split point in `mmTitleMstarWqvga.cbm`. `src/main.c` now traces `sub_938` at the points where it writes local login-state byte `+10302`, checks the router gate byte at `router[17/18]`, and calls the router callback at `router+44`; it also traces the local title mini-state transitions at `sub_5B0_set_local_mode2` and `sub_5B0_queue_activate_role_manage`. Build validation succeeded (`make` passed).
- verified: the latest static pass makes the remaining branch shape much clearer. `sub_938(event=7)` does not itself decide “title flow vs scene flow” by subtype alone once it has accepted `2/3/6`. After parsing an accepted object, it fetches a router object from `sub_19C()`, sets module-local byte `*(R9+10302)=3/4`, checks gate byte `router[14 + state]` (i.e. `router[17]` or `router[18]`), and if that byte is not `1` it calls `router+44(a1, a3)`. Separately, `sub_A50()` initializes local state `+10340/+10344` to zero, while `sub_840()` only enters the title-local post-login mini-flow when `+10344` later becomes `1` or `2`.
- implication: the remaining mismatch is likely no longer “wrong success subtype” but “wrong router/gate state before the router callback fires”. If the expected pre-map title workflow is still missing, the absent condition is most plausibly one of:
  - the router gate byte `router[17]` / `router[18]` should already be `1`, suppressing the direct callback
  - or some earlier title-local path should have moved `+10344` into the `1/2` mini-state machine before scene-owned routing takes over
- next: rerun once with the new trace and inspect `trace_title_router_gate` around the first `1/1/2` or `1/1/3` success. The concrete next question is whether the direct-enter callback at `router+44` is firing because the gate byte is `0`, or because the title-local `+10344` flow never becomes active at all.

## 2026-06-04
- verified: the final sibling test closes the loop: isolated `1/1/3` also converges on the same direct-enter path. In the latest session window (`account=321` at `tick=231`), the login reply is `route=actor-subtype-3`, the raw packet begins `575401680101010300...`, queued `1/1/15` remains disabled, yet `010534b4` is still removed at `tick=232`, `01053450` is created at `tick=234`, and `businessDispatch` again switches from `05017509` to `01012e4d`.
- verified: subtype `3` uses the same narrower business field set as subtype `2`: `result, revivetype, ruffianflag, type, lastexp, curexp, persentexp, actorinfo`, with the same `len=360` payload size. In the current emulator path it does not produce a distinct title-stage behavior from subtype `2`; it still resumes the same `01053450` scene/bootstrap sequence and later lands in `sceneState=7` with `loadFlags=1,0,0`.
- evidence: `bin/logs/net_trace.log` lines around `mock_login_response_experiment requestSubtype=1 route=actor-subtype-3`, `mock_login_actor_response_fields top=1,1,3`, `skip_login_followup_event ... queueMode15=0`, `screen_manager idx=6 remove r0=010534b4`, `screen_manager idx=4 add r0=01053450`, and `trace_business_callback_write ... new=01012e4d`; `bin/logs/net_packets.log` raw login success dump beginning `575401680101010300...`.
- next: stop treating subtype choice among `2/3/6` as the deciding factor for staying in title flow. All three `sub_938`-accepted success subtypes currently converge on the same `010534b4 -> 01053450 -> sceneState=7` route, so the remaining protocol gap is more likely an additional title-stage gate, companion packet, or earlier mode/owner contract that is still absent from the emulator path.

## 2026-06-04
- changed: temporarily rebased the experiment binary again so the built-in fallback login subtype is now `3` instead of `2`. `vm_net_mock_resolve_login_experiment_subtype()` still honors explicit `CBE_LOGIN_TOP_TYPE`, but when no environment override is present the rebuilt `bin/main.exe` now defaults to `1/1/3`. Build validation succeeded (`make` passed).
- next: rerun once without extra env overrides for the clean subtype-3 baseline, then compare whether `route=actor-subtype-3` converges on the same `010534b4 -> 01053450 -> sceneState=7` path already confirmed for subtypes `6` and `2`.

## 2026-06-04
- verified: the rebuilt default-`2` binary confirms that `1/1/2` behaves the same way as isolated `1/1/6` on the current stuck title path. In the later session window (`account=123`), the login reply is `route=actor-subtype-2`, the raw packet begins `575401680101010200...`, queued `1/1/15` is still disabled (`skip_login_followup_event ... queueMode15=0`), yet `010534b4` is still removed at `tick=167`, `01053450` is created at `tick=169`, and `businessDispatch` again switches from `05017509` to `01012e4d`.
- verified: the subtype-2 business payload is narrower than subtype-6 exactly as expected from the new field trace. This run logs `fields=result,revivetype,ruffianflag,type,lastexp,curexp,persentexp,actorinfo`, and the packet shrinks from the earlier subtype-6 `len=432` to `len=360` because the subtype-6-only `practiseflag/pcimg/expcard/expbook/practiseinfo` entries are absent.
- verified: despite that payload difference, the post-jump business sequence is unchanged in the current emulator path. After `1/1/2`, the client still proceeds into the same `01053450` scene bootstrap and reaches `sceneState=7`, `loadFlags=1,0,0`, with the same later built-in replies (`1/5/10 + 1/7/7(type=1)`, then `1/7/7(type=2)` and `type=3`).
- evidence: `bin/logs/net_trace.log` lines around `mock_login_response_experiment requestSubtype=1 route=actor-subtype-2`, `mock_login_actor_response_fields top=1,1,2`, `skip_login_followup_event ... queueMode15=0`, `screen_manager idx=6 remove r0=010534b4`, `screen_manager idx=4 add r0=01053450`, and `trace_business_callback_write ... new=01012e4d`; `bin/logs/net_packets.log` raw login success dump beginning `575401680101010200...`.
- next: `1/1/3` is now the remaining sibling subtype to compare. If it also tears down `010534b4` and enters the same `01053450` bootstrap, then all three `sub_938`-accepted success subtypes (`2/3/6`) likely converge on the same current direct-enter route in this emulator path, and the real remaining question becomes which additional title-side gate or companion packet is missing to keep the client in the expected pre-map workflow.

## 2026-06-04
- changed: temporarily rebased the experiment binary so the built-in fallback login subtype is now `2` instead of `6`. `vm_net_mock_resolve_login_experiment_subtype()` still honors explicit `CBE_LOGIN_TOP_TYPE`, but when no environment override is present the rebuilt `bin/main.exe` now defaults to `1/1/2`. Build validation succeeded (`make` passed).
- next: rerun without additional env overrides if we want a clean subtype-2 baseline from this binary, then compare `route=actor-subtype-2` against the already confirmed pure-`1/1/6` behavior.

## 2026-06-04
- verified: queued `1/1/15` is **not** required for the current `1/1/6 -> 01053450` jump. With the new isolated control (`skip_login_followup_event request=1/1/1 label=title-mode15 queueMode15=0`), the first clean `CBE_LOGIN_TOP_TYPE=6` rerun still removed `010534b4` at `tick=177`, installed `01053450` by `tick=179`, and switched `businessDispatch` from `05017509` to `01012e4d`. The later scene-bootstrap sequence was unchanged: the client immediately sent bundled `1/5/10 + 1/7/7(type=1)`, then standalone `1/7/7(type=2)` and `1/7/7(type=3)`, and runtime again reached `sceneState=7` with `loadFlags=1,0,0`.
- verified: the isolated `1/1/6` business payload on this run contains the richer subtype-6-only fields, matching the new trace output: `result, revivetype, ruffianflag, type, practiseflag, pcimg, expcard, expbook, practiseinfo, lastexp, curexp, persentexp, actorinfo`.
- evidence: `bin/logs/net_trace.log` lines around the single `mock_login_response_experiment requestSubtype=1 route=actor-subtype-6 ...`, `skip_login_followup_event ... queueMode15=0`, `screen_manager idx=6 remove r0=010534b4`, `screen_manager idx=4 add r0=01053450`, and the later `send_payload len=35` / `len=19` scene bootstrap requests; `bin/logs/net_packets.log` raw login success dump beginning `575401b001010106...`.
- next: the intended `1/1/2` and `1/1/3` comparisons are still pending; the latest logs only contain the subtype-6 run. Next reruns should set `CBE_LOGIN_TOP_TYPE=2` and `3` separately and then compare whether either subtype avoids the immediate `01053450` scene/bootstrap transition or changes the later request family.

## 2026-06-04
- changed: split the current login-subtype experiment into explicit controls so `1/1/6`, `1/1/2`, and `1/1/3` can be compared without editing code each time. For non-`1234` `requestSubtype==1` success replies, the mock now chooses subtype from `CBE_LOGIN_TOP_TYPE` (allowed experiment values `2/3/6`, fallback `6`) and logs both the chosen route and the exact business fields via `mock_login_response_experiment ... fields=...` plus `mock_login_actor_response_fields ...`.
- changed: isolated the “only send the main login success object” case for the current `1/1/1` title experiment. For `requestKind==1 && requestSubtype==1`, queued `1/1/15` is now disabled by default; the log prints `skip_login_followup_event request=1/1/1 label=title-mode15 queueMode15=0`. If a later comparison needs the old behavior back, it can be re-enabled with `CBE_LOGIN_QUEUE_MODE15=1`. Build validation succeeded (`make` passed).
- next: run three clean comparisons on the same login path: `CBE_LOGIN_TOP_TYPE=6`, `2`, and `3`, with `CBE_LOGIN_QUEUE_MODE15` left unset or `0`, and compare whether each case still replaces `010534b4`, what new `businessDispatch` gets installed, and whether the follow-on scene/group packets differ.

## 2026-06-04
- verified: the narrowed `1/1/6` experiment finally moved the stuck title screen. On the latest non-`1234` login (`account=4321`), the mock emitted `mock_login_response_experiment requestSubtype=1 route=actor-subtype-6` followed by `mock_login_actor_response ... top=1,1,6 ... len=432`, and the runtime immediately stopped treating `010534b4/sub_938` as the terminal state. At `tick=183`, after the queued `title-mode15` follow-up and the `1/1/6` data event, the host removed `010534b4`, unwound `0105a814`, and then created a new module screen `01053450` whose callback owner is `businessDispatch=01012e4d`.
- verified: this is the first confirmed local transition caused by the post-submit success family. Even though the old `trace_title_login_state` snapshot fields on base `01050bd0` stayed unchanged during the `010534b4` data-event window, the success criterion has now been met at the screen-manager level: `1/1/6` caused a real active-screen replacement and scene progression (`sceneState=7`, `loadFlags=1,0,0`) that never happened under the earlier `1/1/1` replies.
- evidence: `bin/logs/net_trace.log` around `tick=182..186`, especially `mock_login_response_experiment`, `mock_login_actor_response actorinfo_len=232 top=1,1,6`, `screen_manager idx=6 remove r0=010534b4`, `screen_manager idx=4 add r0=01053450`, and `trace_business_callback_write ... new=01012e4d`; `bin/logs/net_packets.log` raw response dump beginning `575401b001010106...`, which confirms the object header really is `1/1/6` even though the current packet-summary line still prints `objs=1/1/1`.
- next: stop questioning the `010534b4 -> subtype 6` contract. The next narrow target is the new `01053450` screen and its `businessDispatch=01012e4d` owner: identify what this screen expects next and whether the follow-on `builtin-group-type1` request/response pair is sufficient to keep scene initialization moving.

## 2026-06-04
- changed: narrowed the next live experiment to the `sub_938`-accepted family. For non-`1234` accounts, the default successful `requestSubtype==1` mock path now returns the existing actor-style `1/1/6` success object instead of the older `1/1/1 serverinfo/servernum/newVer` validation packet. The legacy `1/1/1` control path is still available with `CBE_LOGIN_RESPONSE=primary-validate` for side-by-side regression runs. Build validation succeeded (`make` passed).
- evidence: `src/main.c` updated `vm_net_mock_build_login_response()` branch selection plus the new `mock_login_response_experiment requestSubtype=1 route=actor-subtype-6` trace marker.
- next: rerun once and compare `bin/logs/net_packets.log` / `bin/logs/net_trace.log` for the first non-`1234` confirm. The narrow success criterion is no longer “did the wire packet arrive”, but whether `1/1/6` finally causes any local title writes or `trace_title_screen_callback` activity while `businessDispatch` remains `05017509`.

## 2026-06-04
- verified: the local confirm button path does not perform the hoped-for owner swap. Static `mmTitleMstarWqvga.cbm` analysis now ties the visible confirm action to `login_form_handle_action(4096) -> login_form_submit()`, and that submit path only copies the current buffers into `mmorpg_LoginRecord`, writes stage byte `*(R9+0x2054 + 357) = 5`, and sends `net_build_login_request(1, 1, 6)` which appears on the wire as top-level `1/1/1`. It does **not** write `businessDispatch`, and the new runtime watcher confirms there are no further `trace_business_callback_write label=businessDispatch` lines after the initial `sub_A50()` install at `tick=78`.
- verified: this means the current question has shifted again. The stuck path is not “confirm should have switched to `login_primary_response_dispatch()` but didn't”; on this screen, confirm appears to intentionally leave `businessDispatch = sub_938 + 1` in place. The more plausible contract is now that `sub_938(event=7)` is itself the intended owner after submit, and that the server reply subtype should match one of its accepted `type=1/subtype=2|3|6` objects rather than the direct `1/1/1` primary-wrapper path.
- evidence: `mmTitleMstarWqvga.cbm` IDA for `login_form_handle_action()` (`0x2B1E`), `login_form_submit()` (`0x1E40`), `sub_A50()` (`0x0A50`), and `sub_938()` (`0x0938`); latest `bin/logs/net_trace.log` lines for confirm taps at `x=38..47 y=373..380`, repeated `send_call connect=2 len=95`, and the absence of any post-submit `trace_business_callback_write label=businessDispatch` after the earlier `tick=78 last=050176c2` install.
- next: stop searching for a missing local callback swap on this specific confirm path. The next narrow experiment should compare `sub_938`'s accepted subtype family against older `1/1/6` behavior and verify whether the correct post-submit reply contract for `010534b4` is actually `type=1/subtype=6` (or sibling `2/3`) rather than `1/1/1`.

## 2026-06-04
- changed: added one more read-only runtime watcher for the shared business callback slots. `src/main.c` now logs `trace_business_callback_write` whenever guest code writes `Global_R9 + 21804` (`businessCtx`) or `Global_R9 + 21812` (`businessDispatch`), so the next rerun can show exactly which guest PC installs the active title-business callback without relying only on later snapshots. Build validation succeeded (`make` passed).
- verified: the current stuck login owner is no longer ambiguous. The active screen `010534b4` is the `mmTitleMstarWqvga.cbm::sub_C82()` transition screen, with runtime function table entries matching `sub_A50/sub_8E0/sub_840/sub_59E/sub_4A4/sub_454` under relocation base `0x05016BD0`. Its live `dispatch=05017509` snapshot is `sub_A50()`'s installed callback `sub_938 + 1`, not `login_primary_response_dispatch()` or `login_alt_response_dispatch_wrapper()`.
- verified: `sub_938(event=7)` only scans `type=1` packet objects whose `subtype` is `2`, `3`, or `6`. It never checks `subtype == 1`, so the current `1/1/1 {result=2, information}` reply is being delivered to an owner that ignores it by construction. This explains the unchanged `trace_title_login_state` snapshots and the continued absence of `trace_title_screen_callback`.
- verified: the primary fixed failure prompt does not need an additional stage packet. Static `net_handle_login_response(packet, 1)` already feeds `login_response_result_dispatch('2')` from the parsed `result` byte alone; the `information` copy helper only runs on the alternate `a2 == 0` path. So the blocker is still owner selection / callback installation timing, not a missing companion object after `1/1/1 result=2`.
- evidence: `mmTitleMstarWqvga.cbm` IDA for `sub_C82()` (`0x0C82`), `sub_C54()` (`0x0C54`), `sub_A50()` (`0x0A50`), `sub_938()` (`0x0938`), `net_handle_login_response()` (`0x16DC`), `login_primary_response_dispatch()` (`0x2D80`), and `login_response_result_dispatch()` (`0x23C0`); latest `bin/logs/net_trace.log` `screen_func_table screen=010534b4 ... init=05017621 destroy=050174b1 logic=05017411 render=0501716f pause=05017075 resume=05017025` plus repeated `runtime_state ... dispatch=05017509`.
- next: rerun once and capture the new `trace_business_callback_write` lines around the `01056204 -> 0105A814 -> 010534B4` transition and the confirm-tap path. The remaining narrow question is not "what should `1/1/1 result=2` contain", but "which earlier local mode/owner switch is supposed to replace `sub_938` with the real login-result wrapper before the account request is sent".

## 2026-06-03
- verified: the new `trace_title_screen_callback` settled the “点确认后没有任何反应” split. On the latest `1/1/1` primary-validation runs, the login request really leaves (`send_call connect=2 len=94 ...`), the host really returns the `1/1/1 result/serverinfo/servernum/newVer` packet (`mock_login_primary_validation_response ... len=93`), and the queued follow-up `title-role-stage4` packet also fires, but **none** of the four title-side local transition BLX sites (`0x23D4`, `0x248A`, `0x1984`, `0x1994`) are reached at all: there is no `trace_title_screen_callback` line in the session. In the same window there is also no host-side `screen_manager idx=2/4/6` transition after the login response; `activeScreen` stays `010534b4`, and repeated confirm taps simply generate repeated `1/1/1` login requests.
- evidence: latest `bin/logs/net_trace.log` lines around the `tick=242/243` and `tick=295/296` login exchanges; absence of any `trace_title_screen_callback` lines despite the newly compiled hook; persistent `runtime_state ... activeScreen=010534b4 currentThis=0105349c`; latest `bin/logs/net_packets.log` showing repeated `1/1/1` request -> `1/1/1` validation response blocks.
- next: stop assuming the current active title screen is already routing `1/1/1` into `login_primary_response_dispatch()` / `0x23C0`. The next narrow static task is to trace dispatcher `05017509` for screen `010534b4` and identify which packet family it actually treats as a successful local transition trigger.

## 2026-06-03
- changed: added a small read-only runtime trace for the title-side local screen transition callsites. `src/main.c` now logs `trace_title_screen_callback` when execution reaches the callback BLX sites at `0x23D4`, `0x248A`, `0x1984`, and `0x1994`, capturing the router object, target object, method pointer, and current active screen so later runs can distinguish “login/role callback never fired” from “callback fired but no host-side screen-manager change followed”. Build validation succeeded (`make` passed).
- evidence: `src/main.c` `vm_net_trace_title_screen_callback()` plus the new hook dispatch cases for the four BLX callsites; successful `make`.
- next: on the next reproduction, compare `trace_title_screen_callback` against nearby `screen_manager idx=2/4/6` lines. If the callback trace never appears, the title parser never reached the local transition. If it appears but there is still no `screen_manager` change, the router target itself is stalling inside the same local screen.

## 2026-06-03
- verified: the primary login-success callback target is now clearer. `login_response_result_dispatch('1')` does not call an opaque bare function at `*(r9+10464)`; the actual call is `R9+0x28CC -> method +0x14` with target `*(R9+0x29E8 + 0x4C)`. The sibling role-list success path `sub_247A()` uses that same router method with target `*(R9+0x29E8 + 0x50)`. Static screen-state code strongly suggests these are local screen/state targets, not network callbacks: `sub_4290()` routes by `*(r9+10748)` (`0 -> sub_3ECE` server-select navigation, `1 -> sub_4016` role-select confirm, `2 -> sub_3FD6` editor/create-name), `sub_3AD2()` resets the family to mode `0`, `sub_39A4()` switches it to mode `1`, and the confirmed role-list parser `sub_3544()` finishes by calling `sub_247A()`.
- verified: the account/password login form really does default the "保存为默认帐号" state byte on the visible form path. `login_form_init()` (`0x2AA8`) explicitly sets `*(r9+10735) = 1`, matching the checkbox being checked by default on that specific UI.
- evidence: `mmTitleMstarWqvga.cbm` static analysis of `login_response_result_dispatch` (`0x23C0`), `sub_247A` (`0x247A`), `login_form_init` (`0x2AA8`), `sub_4290` (`0x4290`), `sub_3AD2` (`0x3AD2`), `sub_39A4` (`0x39A4`), and `sub_3544` (`0x3544`).
- next: if saved-account persistence still appears absent on a live `1/1/1` main-success run, the remaining suspicion is no longer the checkbox default on the login-form path. The next narrow check would be whether the active screen is really `login_form_init()`'s form path or another login variant, and whether `login_primary_response_dispatch()` is actually reached on that run.

## 2026-06-03
- verified: the new `1/1/1` primary-validation reply now matches the static login-success control flow much better, and it also explains the user's two observations. On the primary chain, `login_primary_response_dispatch()` (`0x2D80`) first calls `net_handle_login_response(packet, 1)`, which for `result=='1'` parses `serverinfo/servernum/newVer` plus optional `color/titleimgs`, but it does **not** copy `username/password` on success and does **not** itself send any follow-up network request. Back in `0x2D80`, saved-account persistence only happens if both `loginState[1] == '1'` and local byte `*(r9+10735) == 1`; if that UI flag is not set, no save helper is called. The final step is `login_response_result_dispatch('1')`, which only invokes the generic next-step callback at `*(r9+10464)()` rather than directly building another packet.
- evidence: `mmTitleMstarWqvga.cbm` static analysis of `login_primary_response_dispatch` (`0x2D80`), `net_handle_login_response` (`0x16DC`), `login_response_result_dispatch` (`0x23C0`), and `login_record_save_default_credentials` (`0x1430`); latest `bin/logs/net_packets.log` showing the new `1/1/1` validation reply with `serverinfo/servernum/newVer` and no later business packet automatically following it.
- next: treat “no automatic later request” as expected behavior for this stripped validation packet unless the success callback's target screen/state itself emits one. If we want to validate saved-account persistence, we need to confirm the relevant UI flag/checkbox path sets `*(r9+10735) == 1` before the login-success response returns.

## 2026-06-03
- changed: split the built-in account-login `1/1/1` reply away from the old direct-enter actor-success payload. `src/main.c` now defaults the normal `1/1/1` login branch to a `mock_login_primary_validation_response` packet: one `1/1/1` object carrying only `result`, `serverinfo`, `servernum`, and `newVer`. The previous `actorinfo`-bearing direct-enter payload is still available through explicit non-default login modes, but the default branch no longer injects `actorinfo` during account login.
- verified: build validation succeeded after the new primary-chain validation response was introduced (`make` passed). No new runtime reproduction has been run yet.
- evidence: `src/main.c` `vm_net_mock_build_login_serverinfo_blob()`, `vm_net_mock_build_login_primary_validation_response()`, updated `vm_net_mock_build_login_response()`, and successful `make`.
- next: rerun and confirm that `bin/logs/net_packets.log` now shows `source=builtin-login` with `request 1/1/1 -> response 1/1/1` and the new `mock_login_primary_validation_response` marker, then check whether `net_handle_login_response()` side effects become visible without immediately falling into the old direct-enter `actorinfo` path.

## 2026-06-03
- changed: normalized the built-in account-login success object back toward the real request stage. `src/main.c` now resolves login-success object subtype from the actual request subtype by default, instead of hard-defaulting the actor-success object header to `1/1/6`. The new helper `vm_net_mock_resolve_login_success_subtype()` keeps explicit `CBE_LOGIN_TOP_TYPE` overrides available for targeted experiments, but a normal `1/1/1` account-login request will now receive a `1/1/1` success object and therefore can hit title wrapper `0x2D80`.
- verified: build validation succeeded after the login-success subtype fix (`make` passed). No new runtime reproduction has been run yet.
- evidence: `src/main.c` `vm_net_mock_resolve_login_success_subtype()`, updated `vm_net_mock_build_login_actor_response()` / `vm_net_mock_build_login_actor_role_list_response()`, and successful `make`.
- next: rerun and confirm in `bin/logs/net_packets.log` that the `source=builtin-login` account-login exchange now shows `request 1/1/1 -> response 1/1/1`, then check whether `net_handle_login_response()` side effects such as `serverinfo/servernum/newVer` parsing and saved-account handling finally become observable on the primary chain.

## 2026-06-03
- verified: the current "no `net_handle_login_response()` effects" symptom has two different causes depending on which login branch is live. On the account-login branch, one captured `source=builtin-login` exchange handled request `1/1/1` but answered with response object `1/1/6`; that bypasses title wrapper `0x2D80`, so `net_handle_login_response()` is never called and none of its `serverinfo/servernum/newVer` or saved-credentials side effects can occur. On the alternate direct-enter branch, `1/1/12` does hit wrapper `0x2A50` and therefore does call `net_handle_login_response()`, but the current mock only returns `result=1`, so there is still no `serverinfo/servernum/newVer` payload to consume, and the later default-account persistence logic is absent because it lives in `0x2D80`, not `0x2A50`.
- evidence: `bin/logs/net_packets.log` account-login block (`request 1/1/1`, response header `1/1/6`) and direct-enter block (`request 1/1/12`, response header `1/1/12`); `src/main.c` `vm_net_mock_build_login_response()` / `vm_net_mock_build_login_actor_response()`; title-module wrappers `0x2D80` and `0x2A50`; `net_handle_login_response()` at `0x0016DC`.
- next: if the goal is to exercise the real account-login parser path, the built-in response for `1/1/1` needs to be normalized back to `type=1, subcmd=1` instead of the current `actorSubtype=6` object header. If the goal is to exercise server-list parsing on the `1/1/12` path, the mock must also start returning real `serverinfo/servernum/newVer` fields there rather than only `result=1`.

## 2026-06-03
- verified: the post-login bundled request `1/5/10 id=10001 + 1/7/7 type=1` is now pinned to the first scene bootstrap, not the title/login UI. Runtime `bin/logs/net_trace.log` places the `len=35` send immediately after `trace_sub_1010228_callsite label=scene_runtime_init_and_sync ... activeScreen=01053450`, and static `scene_runtime_init_and_sync()` at `0x01013594..0x01013636` shows the same sequence: call `sub_1010228()`, then queue three outgoing `major=1, kind=7, subtype=7` requests with field `type = 1..3`.
- verified: `sub_1010228()` is the local pre-send helper that explains why `type=1` is bundled with group info. Static analysis shows it rebuilds the 0x130-byte local group snapshot at `*(R9+0x5CE4+0x10)` from `sceneCurrentNode`, clears the cached leader id, and sets the local send/state flags at `R9+0x5C74` and `R9+0x5C79` to `1`. That matches runtime, where only the first of the queued `type=1/2/3` requests leaves as the combined `1/5/10 + 1/7/7(type=1)` packet.
- verified: the current best business reading is now narrower than the older generic "game type" wording. `1/7/7` is a scene-side misc-player-fields bootstrap request family emitted during first scene init: `type=2` maps to the already-confirmed `pcimg` consumer (`top-level 7/20`), `type=3` maps to the `expcard` consumer (`top-level 7/32`), and `type=1` is the broad initial sync member whose true top-level `7` reply is still unresolved.
- evidence: `bin/logs/net_trace.log` lines around the `len=35/19/19` sends; `scene_runtime_init_and_sync()` disassembly at `0x01013594..0x01013636`; `sub_1010228()` at `0x01010228`; `net_handle_misc_player_fields()` at `0x01011C88`; `docs/re/protocol.md` post-login section.
- next: stop looking for the source of `1/7/7` in the title module. The next narrow protocol task is to recover the real top-level `7` reply for the scene-bootstrap `type=1` member, while keeping `type=2 -> pcimg` and `type=3 -> expcard` as confirmed anchors.

## 2026-06-03
- changed: reshaped `logs/net_packets.log` into per-request grouped blocks instead of interleaving independent packet/body lines. For handled requests, the file now writes one contiguous sequence: `游戏请求数据包` -> request payload dump -> `主机处理信息 ...` -> `主机响应数据包` -> response payload dump. For unknown requests it now writes the same first two sections and ends the response section with `assert(0)`.
- verified: build validation succeeded after the grouped packet-log formatting change (`make` passed). No new runtime reproduction has been run yet.
- evidence: `src/main.c` `vm_net_packet_log_exchange()`, updated `vm_net_log_handled_packet()` / `vm_net_log_unhandled_packet()`, and `vm_net_mock_on_send()`; local rebuild result from `make`.
- next: on the next run, `net_packets.log` should read as one request/response transaction per block, so the first unhandled packet can be copied directly without reconstructing order from interleaved lines.

## 2026-06-03
- changed: moved raw packet/body dumps out of `logs/net_trace.log` and into the dedicated packet channel. `send_payload` and `mock_response_payload` are now written through `vm_net_packet_trace_bytes()` to `logs/net_packets.log`, while `net_trace.log` keeps the higher-level scheduling, dispatch, and state lines.
- verified: build validation succeeded after the logging split (`make` passed). No new runtime reproduction has been run yet.
- evidence: `src/main.c` `vm_net_packet_trace_bytes()` and the updated `vm_net_mock_on_send()` callsites for `send_payload` / `mock_response_payload`; local rebuild result from `make`.
- next: on the next run, use `net_trace.log` for timeline/state and `net_packets.log` for exact request/response bodies. That should make the first unhandled packet easier to isolate without paging through large hex dumps in the state log.

## 2026-06-03
- changed: tightened host-side mock packet dispatch so recognized requests are now explicitly classified and logged, while unknown requests no longer fall through to broad default behavior. `src/main.c` now emits `handled_packet` summaries for every matched built-in/file/rule branch, mirrors those summaries into a dedicated `logs/net_packets.log` channel, and removes the old trailing `default.bin` / `echo-wt` / `OK` fallbacks in favor of `assert(0)` on unrecognized packets.
- verified: build validation succeeded after the dispatcher/logging change (`make` passed). No new runtime reproduction has been run yet, so the main effect of this change is observability and a stricter failure mode for the next unknown packet.
- evidence: `src/main.c` `vm_net_packet_trace()`, `vm_net_packet_build_request_summary()`, `vm_net_log_handled_packet()`, `vm_net_log_unhandled_packet()`, and the updated `vm_net_mock_build_response()` return paths; local rebuild result from `make`.
- next: on the next reproduction, use `logs/net_packets.log` as the canonical “host recognized this packet” timeline. The first new `assert(0)` on an unhandled request should now identify the next real protocol gap directly instead of silently echoing through it.

## 2026-06-03
- verified: after gating the risky `1/7/8` experiment back off by default, the next reproduction cleanly returns to the previous later blocker instead of the earlier `sub_1033544()` null-copy fault. `net_trace.log` now shows `mock_group_type1_response ... objects=5 miscSync8=0 len=467`, then the normal `kind=10/subtype=26` and `kind=30/subtype=1/3/7` scene-channel consumption, and finally the same HUD rebuild/draw failure: `trace_status_meter_rebuild_site label=seed_gate ... sourceHead=00000000` followed by `trace_status_bar_divide_site label=primary ... baseMax=120 displayMax=0`.
- verified: a quick static sweep of the already-named `top-level 7` family helpers used by `net_handle_misc_player_fields()` does not suggest an obvious alternative producer for the null `*(R9+38020)` / `*(R9+38024)` local pointers. `sub_101191A()` (`subtype 37`) handles `itemid/seq/itemname`, `sub_10118E0()` (`38`) handles `bookdes`, `sub_1011834()` (`41`) handles `hint/result`, and `sub_1011A1E()` / `sub_1011A5E()` handle `bookinfo` / `expinfo`; all look like UI/message/book flows rather than constructors for the pending 324-byte records consumed by `sub_1033544()` subtype `8/9`.
- evidence: latest `bin/logs/net_trace.log` lines around `5451..5452`, `5484`, `5718..5731`, and `5755`; latest `bin/logs/stdout_trace.log`; static decompilation of `sub_101191A`, `sub_10118E0`, `sub_1011834`, `sub_1011A1E`, and `sub_1011A5E`.
- next: narrow the search away from the generic `7/31/36/37/38/41/42` UI helpers and toward the specific setup path for the two 324-byte pending records at `*(R9+38020)` / `*(R9+38024)`, because those are now the proven prerequisite before any safe `subtype=8/type=4` success reply can work.

## 2026-06-03
- verified: the first `1/7/8` misc-player-fields experiment produced a useful negative result, but not a safe default fix. Runtime now shows the bundled post-login reply really can deliver `kind=7 subtype=8` (`mock_group_type1_response ... objects=6 miscSync8=1`, then `trace_business_handler pc=01011c88 kind=7 subtype=8`), yet the crash moves earlier into `sub_1033544()` itself: `stdout_trace.log` faults at `lr=0x010335A9`, `pc=0x0104D430`.
- verified: that new fault is not another HUD divide. `0x0104D430` is `sub_104D424()`, the 324-byte copy helper called from the `subtype=8/type=4/result=1` branch. The register dump (`r1=0`) and static code together show the branch is trying to clone `*(R9+38020)` before producing the `type=15` bucket, but that local source pointer is still null on the current post-login path. In other words, the packet hit the hoped-for producer branch, but it hit it before its required local pending object existed.
- changed: because that makes `1/7/8` a regression on the default path, `src/main.c` now keeps the experiment behind an explicit environment gate instead of sending it unconditionally. The bundled `group/type1` reply is back to the prior default unless `CBE_GROUP_TYPE1_MISC_SYNC8=1` is set.
- evidence: latest `bin/logs/net_trace.log` lines around `2304..2308`, `2337`, and `2571..2572`; latest `bin/logs/stdout_trace.log`; `sub_1033544()` branch at `0x0103596..0x01035B6`; `sub_104D424()` at `0x0104D424`.
- next: stop treating `subtype=8/type=4` as a safe generic answer to the bundled `1/7/7 type=1` request. The next narrow task is to identify which earlier request/response stage seeds `*(R9+38020)` and `*(R9+38024)`, or to prove that the real answer to `1/7/7 type=1` is a different top-level `7` subtype entirely.

## 2026-06-03
- changed: `src/main.c` now turns the previous protocol suspicion into a focused runnable experiment. `vm_net_mock_build_group_type1_response()` still answers the post-login bundled request (`1/5/10 id=...` + `1/7/7 type=1`) with group info, the legacy `0x0A/0x1A` role-login object, and scene `0x1E/1/3/7`, but it now also appends one real top-level `7` family object: `1/7/8 {type=4, result=1, seq=1}`. The new helper is `vm_net_mock_append_misc_player_sync8_object()`, and the response log now prints `miscSync8=1`.
- verified: the reason for choosing `1/7/8` is narrower than a generic “try subtype 8” guess. Static `sub_1033544()` analysis already shows that top-level `7`, packet-local subtype `8`, parsed `type=4, result=1` is one of the very few branches that both calls the manager `+0x68` producer (`sub_1032B8A()`) and, when `R9+0x6048` is empty, immediately performs the safe `scene_rebuild_status_meter_node(2) -> scene_rebuild_status_meter_node(1)` seed sequence.
- verified: build validation succeeded after the change (`make` passed). No new runtime reproduction has been run yet, so this entry is a protocol experiment setup, not a confirmed fix.
- evidence: `src/main.c` `vm_net_mock_append_misc_player_sync8_object()` and `vm_net_mock_build_group_type1_response()`; static `sub_1033544()` branch at `0x0103588..0x01035F0`; local rebuild result from `make`.
- next: rerun and check for three specific signals in `bin/logs/net_trace.log`: `mock_group_type1_response ... miscSync8=1`, a new `trace_business_handler ... kind=7 subtype=8`, and whether `trace_status_meter_rebuild_site label=seed_gate` still shows `sourceHead=00000000` before the first HUD draw.

## 2026-06-03
- verified: the newest rerun reconfirms the current strongest protocol gap without opening a new branch. After the post-login `send_payload len=35` bundle (`1/5/10 id=10001` + `1/7/7 type=1`), the built-in reply is still `mock_group_type1_response ... objects=5 len=467`, and the subsequent dispatch still shows only `kind=10/subtype=26` plus `kind=30/subtype=1/3/7`. There is still no top-level `kind=7` response object at all before the first HUD draw.
- verified: the local failure chain remains identical in the same session: `trace_status_meter_rebuild_site label=seed_gate` still reports `n2=1 sourceHead=00000000 currentDisplay=0/0 meterDisplay=0/0`, and the later draw still faults at `trace_status_bar_divide_site label=primary ... baseMax=120 displayMax=0`.
- evidence: latest `bin/logs/net_trace.log` lines around `2338..2340`, `2371`, `2605..2618`, and `2642`; latest `bin/logs/stdout_trace.log` session tail.
- next: the next narrow protocol task is now explicit: stop treating the combined `len=35` request as “already answered” just because `group/type1/scene` objects were returned. The missing experiment is to answer the bundled `1/7/7 type=1` half with at least one real top-level `7` family object, then observe whether that finally creates the `type=15` source head before the first HUD draw.

## 2026-06-03
- verified: there was an important layer-mismatch in the earlier wording. `sub_1033544()` is not a top-level `kind=8/9` business handler. `net_business_response_dispatch()` shows it is reached only through top-level business subcommand `7` (`net_handle_misc_player_fields()`), and inside that family `sub_1033544()` then switches on packet-local subtype `1/4/8/9/11/12`. So the HUD-meter producer branches are more accurately “subtype `8/9` under top-level `7`”, not standalone top-level kinds.
- verified: the current post-login `len=35` client request already contains exactly such a top-level `7` family object, but the mock is only answering the other half of the combined request. The request contains both `1/5/10 id=10001` and `1/7/7 type=1`; however, `vm_net_mock_build_group_type1_response()` currently uses that `type=1` only as a boolean gate to append `0x0A/0x1A` plus `0x1E/1/3/7`, and still returns no `1/7/*` response object at all.
- verified: that makes the current protocol gap more concrete than the earlier generic “missing business packet” phrasing. On the live path, the client is already explicitly asking for a top-level `7` family sync (`type=1`), but the built-in mock replies with group info, role-login/name-money fields, and scene-enter data only. It does not currently emit any top-level `7` subtype such as `8/9/11/12`, which aligns with the continued absence of a `type=15` meter-source head before first HUD draw.
- evidence: `net_business_response_dispatch()` dispatch table at `0x01012EFA..0x01012F00`; latest `bin/logs/net_trace.log` `send_payload len=35` at lines around `2164..2170`; `src/main.c` `vm_net_mock_build_group_type1_response()` at `3385..3433` and `vm_net_mock_append_type1_object()` at `3280..3297`.
- next: treat the built-in `type=1` response as the strongest current protocol suspect. The next narrow task is to determine which top-level `7` subtype(s) should answer that bundled `1/7/7 type=1` request on a real client path, before changing the mock shape.

## 2026-06-03
- verified: the newest rerun is still the same first-scene divide-by-zero path, now with cleaner timing but no contradictory evidence. `stdout_trace.log` still ends at `Arithmetic exception: Divide By Zero` with `pc=0x0104E980`, `lr=0x01014785`, and the paired `net_trace.log` still reaches `trace_scene_runtime_tick label=status_panels` followed immediately by `trace_status_bar_divide_site label=primary ... current=0 baseMax=120 displayMax=0`.
- verified: the negative protocol-side evidence also held on this rerun. Before the first HUD draw, the fresh-enter burst again contains only `trace_business_handler kind=10/subtype=26` and `kind=30/subtype=1,3,7`; there is still no `kind=8`, no `kind=9`, and no sign of a `net_handle_misc_player_fields()` producer packet that could have populated the manager's `type=15` head at `R9+0x6048`.
- verified: the empty-head rebuild trace remains unchanged in the same session. `trace_status_meter_rebuild_site label=seed_gate` still fires at `tick=468` with `n2=1`, `sourceHead=00000000`, `currentBase=120/100`, and `currentDisplay=0/0`, so this run reinforces the current interpretation rather than opening a new branch.
- evidence: latest `bin/logs/stdout_trace.log` session tail; latest `bin/logs/net_trace.log` lines around `2199`, `2433..2446`, and `2467..2470`.
- next: stop expecting the current first-scene mock burst to fix this by itself. The next narrow task is to identify which earlier request/response stage is supposed to deliver the missing `kind=8/9` misc-player-field packet(s), or to prove that the first safe scene-enter path should synthesize the same `type=15` head locally before `scene_runtime_init_and_sync()` reaches `rebuild(1)`.

## 2026-06-03
- verified: `R9+0x6048` is no longer just an abstract “source head”. `scene_system_bootstrap()` initializes the whole manager at `R9+0x6040` via `sub_10332BE(R9+0x6040, 64, 74)`, and `sub_1031CF4()` now confirms that head-slot routing maps runtime record `type=15` to manager offset `+8`. So the meter rebuild path is specifically iterating the `type=15` bucket of that scene-local manager, not an arbitrary list.
- verified: the producer side is much narrower than before. In the same manager's method table, `+0x64 = sub_1032BDE` reinserts a copied record without changing its type, while `+0x68 = sub_1032B8A` clones a record, force-writes local byte `+282 = 15`, and then inserts it through the common add path. In other words, `sub_1032B8A()` is a concrete "convert/copy this record into runtime `type=15` bucket" helper.
- verified: the first confirmed packet-side producer for that bucket is `net_handle_misc_player_fields() -> sub_1033544()`. Static disassembly now shows two success paths that call the manager `+0x68` helper and then rebuild the HUD meter: `kind=8` with parsed `type=4, result=1` (`0x0103800..0x010383E`), and `kind=9` with `result!=0` (`0x0103776..0x01037C2`). The `kind=8/type=4` path also contains the already-known empty-head recovery (`if [R9+0x6048]==0 -> rebuild(2) -> rebuild(1)`), which ties the `type=15` producer directly back to the only safe empty-head seed mode seen so far.
- verified: the latest live `net_trace` does not currently show any such producer packet on the corrected first-scene path. Around the fresh-enter burst it logs `kind=10/subtype=26` and `kind=30/subtype=1,3,7`, but no `trace_business_handler` branch that would feed `net_handle_misc_player_fields()` / `sub_1033544()` before the first HUD draw. That makes the “missing upstream business packet for the `type=15` bucket” explanation materially stronger than it was before this pass.
- evidence: `scene_system_bootstrap()` call to `sub_10332BE` at `0x010038A8`; `sub_1031CF4()` bucket mapping at `0x01031CF4`; manager method-table install in `sub_10332BE()` at `0x01033332..0x0103333C`; `sub_1032B8A()` forced `type=15` local write; `sub_1033544()` success paths at `0x0103790..0x01037C2` and `0x0103800..0x010383E`; caller `net_handle_misc_player_fields()` at `0x01011D16`; latest `bin/logs/net_trace.log` fresh-enter lines around `2566..2579`.
- next: stop treating the empty `R9+0x6048` head as a generic scene-init mystery. The next narrow question is now whether the mock/login-scene path is missing the specific pre-scene item/effect/equip-style packet(s) that would have driven `sub_1033544()` into one of those `type=15` producer branches before the first HUD draw, or whether there is another non-packet producer for the same bucket that still has not been identified.

## 2026-06-03
- verified: the follow-up rerun with the extra `writeback_common` hook makes the control-flow picture even cleaner. On the live crashing path there is still only `trace_status_meter_rebuild_site label=seed_gate` (`n2=1`, `sourceHead=0`) and still no later `writeback_early` or `writeback_common` log before `trace_status_bar_state pc=0101021A`. Combined with the static branch at `0x010100EC` (`CMP R5,#1 / BNE 0x010101D4`), this means the empty-head first-scene path is bypassing both explicit display-max writeback blocks entirely.
- verified: that leaves only the final clamp/copy tail active on the live path. The function still reaches `0x0101021A` and copies `sceneStatusMeterNode + 0xC4/+0xC8` back into `sceneCurrentNode + 0xC4/+0xC8`, but because neither earlier writeback block ran and the meter node already held `0/0`, the final copy simply preserves the zero display maxima that later fault in `scene_draw_status_panels()`.
- evidence: latest `bin/logs/net_trace.log` lines around `2331-2332` and `2603`; `scene_rebuild_status_meter_node()` disassembly around `0x010100EC..0x010101EE`.
- next: stop treating this as “some later writeback writes the wrong value”. The next narrow question is now purely upstream: who should have made `R5==1` / non-empty contribution state before the first rebuild, or why first-scene enter is not routed through the one mode (`n2==2`) that handles the empty-head case safely.

## 2026-06-03
- verified: the first rerun with `trace_status_meter_rebuild_site` strongly supports the init-mode mismatch hypothesis. On the live crashing scene-enter path, `trace_status_meter_rebuild_site label=seed_gate` fires at `0x0100FF26` with `n2=1` and `sourceHead=00000000`, while both `sceneCurrentNode` and `sceneStatusMeterNode` already show `baseMax=120/100` but `displayMax=0/0`. That means the first rebuild call reaches the exact gate that would have used the `n2==2` fallback, but skips it because the current path is `n2==1`.
- verified: the later state still matches the divide-by-zero chain without contradiction. After the same rebuild pass, `trace_status_bar_state` at `0x0101021A` still shows `primary=0/120/0 secondary=0/100/0`, and the later divide site is still `trace_status_bar_divide_site label=primary ... baseMax=120 displayMax=0`. Even though the first writeback hook did not fire on this run, the seed-gate evidence plus the unchanged post-rebuild state already make the current live interpretation much stronger: first-scene enter is rebuilding with an empty contribution head and no `n2==2` fallback.
- evidence: latest `bin/logs/net_trace.log` lines around `2412-2413` and `2684`; latest `bin/logs/stdout_trace.log` tail with the same `pc=0x0104E980`, `lr=0x01014785` divide-by-zero.
- next: extend the watcher to the common later writeback block in `scene_rebuild_status_meter_node()` so the next rerun can show the exact accumulator values on the path that currently bypasses `0x010100D2`. After that, the next narrow question is no longer “which denominator is zero”, but “who was supposed to populate `R9+0x6048` before first scene HUD rebuild, or why first-scene enter does not use `n2==2` once when that source head is empty”.

## 2026-06-03
- changed: added a new read-only `trace_status_meter_rebuild_site` watcher in `src/main.c` at `scene_rebuild_status_meter_node()` seed/writeback points (`0x0100FF26` and `0x010100D2`). It logs `n2`, `*(R9+0x6048)`, current-node vs meter-node base/display maxima, and the pending display-max accumulator locals before writeback.
- verified: static analysis now makes that watcher worth running. `scene_node_reset_at()` installs `scene_copy_status_snapshot_node()` as the node callback at `+0x150`, so both `scene_rebuild_runtime_nodes()` and `scene_rebuild_status_meter_node()` copy the full `{baseMax,displayMax}` block from their source node. This means the current live `displayMax==0` fault can propagate cleanly from `sceneStatusSnapshotNode -> sceneCurrentNode -> sceneStatusMeterNode` even when base maxima are non-zero.
- evidence: `scene_node_reset_at()` disassembly at `0x01045A82..0x01045A92`; `scene_rebuild_runtime_nodes()` callback call at `0x0100F7D6`; `scene_rebuild_status_meter_node()` callback call at `0x0100FF20`; watcher code in `src/main.c`; `make` rebuild succeeded after the trace addition.
- next: rerun once and check whether `trace_status_meter_rebuild_site label=seed_gate` shows `n2=1` plus `sourceHead=0`, and whether `label=writeback` still shows `pendingDisplay=0/0` before the later primary divide fault. That will distinguish “missing contribution head” from “non-empty source, but no accumulator path fired”.

## 2026-06-03
- verified: static caller audit now explains why the corrected `1/1/12` path can still reach `scene_draw_status_panels()` with `primaryDisplayMax==0`. `scene_runtime_init_and_sync()` calls `scene_rebuild_status_meter_node(1)` at `0x0101363E`, but inside `scene_rebuild_status_meter_node()` the only built-in fallback that seeds meter display maxima from `sceneCurrentNode` base maxima is guarded by `!*(R9+0x6048) && n2==2` at `0x0100FF26..0x0100FF3E`. The live first-scene path therefore skips that fallback entirely.
- verified: the same function later writes the accumulated locals back into `sceneStatusMeterNode + 0xC4/+0xC8` at `0x010100D2..0x010100E0`, and if no contribution path populated those locals they stay zero. That exactly matches the runtime watcher sequence where `scene_copy_status_snapshot_node()` repairs `primaryBaseMax` to `120`, but `trace_status_bar_state` never sees a non-zero `primaryDisplayMax` before the divide at `0x0101477E`.
- verified: `n2==2` is not dead code. `sub_1033544()` has a recovery-style path that calls `scene_rebuild_status_meter_node(2)` at `0x010335EA` and then immediately `scene_rebuild_status_meter_node(1)` at `0x010335F0`, but only when `*(R9+0x6048) == 0`. The other current callers (`scene_runtime_init_and_sync()`, `sub_1011ACA()`, `sub_101CD78()`, `scene_apply_levelup_status_growth()`) all use `n2==1`.
- evidence: `江湖OL.CBE` disassembly for `scene_runtime_init_and_sync()` (`0x0101363C..0x0101363E`), `scene_rebuild_status_meter_node()` (`0x0100FF26..0x0100FF3E`, `0x010100D2..0x01010222`), `sub_1033544()` (`0x010335DE..0x010335F0`), and the latest `bin/logs/net_trace.log` `trace_status_bar_state` / `trace_status_bar_divide_site` lines around ticks `158..160`.
- next: identify what the `R9+0x6048` head/source actually represents and why the first fresh-enter path reaches `scene_rebuild_status_meter_node(1)` before either that source exists or the `n2==2` reseed path runs. The most likely remaining explanations are a missing first-frame contribution-list seed or an init-time mode mismatch (`1` vs `2`).

## 2026-06-03
- changed: enabled a low-noise `trace_status_bar_state` watcher in `src/main.c` that logs the live primary/secondary `current/baseMax/displayMax` triples whenever the scene HUD meter state changes, so the divide-by-zero path can be correlated with the earlier meter-node state transitions instead of only the faulting divide site.
- verified: the corrected divide-site watcher now gives a trustworthy first result. On the latest rerun, the crash is still the primary bar path (`trace_status_bar_divide_site label=primary pc=0101477e`), but the live values are no longer all bogus zeros: `currentNode=05400000`, `meterNode=0500c5c0`, `current=0`, `baseMax=120`, `displayMax=0`. That narrows the fault to the primary denominator supplied by `sceneStatusMeterNode + 0xC4`, not to the source node's base-max field.
- evidence: `bin/logs/net_trace.log` latest session line `trace_status_bar_divide_site label=primary ... current=0 baseMax=120 displayMax=0`; `bin/logs/stdout_trace.log` latest `Divide By Zero` tail; updated watcher code in `src/main.c`.
- next: rerun once with the new `trace_status_bar_state` watcher and identify where `primaryDisplayMax` first becomes `0` on the live path: whether it is never initialized in `scene_rebuild_status_meter_node()`, or whether it is initialized and later cleared before the first HUD draw.

## 2026-06-03
- changed: corrected the new `trace_status_bar_divide_site` watcher to treat `R9+0x5C64` as the inline scene-HUD state block base instead of reading it as a pointer. The watcher now reads `sceneCurrentNode` from `Global_R9 + 0x5C64 + 0x40` and `sceneStatusMeterNode` from `+0x48`, matching the existing static naming.
- verified: the first divide-site rerun already proved that the new live blocker is the primary status-bar ratio path, not the old `tile26` image dereference. `trace_status_bar_divide_site` fired at `pc=0x0101477E` with all sampled bar values reading as `0`, but the same log also showed obviously bogus node pointers because the first watcher revision mistakenly dereferenced `R9+0x5C64` itself. Those pointer values (`currentNode=68a0e968`, `meterNode=327d22ff`) should therefore be treated as invalid instrumentation output rather than game-state evidence.
- evidence: latest `bin/logs/net_trace.log` line `trace_status_bar_divide_site label=primary pc=0101477e ... current=0 baseMax=0 displayMax=0 ...`; latest `bin/logs/stdout_trace.log` tail with `Arithmetic exception: Divide By Zero` at `pc=0x0104E980`, `lr=0x01014785`; fixed watcher code in `src/main.c`.
- next: rerun once more with the corrected watcher. The next decisive evidence should be real `sceneCurrentNode` / `sceneStatusMeterNode` pointers plus the primary bar's `current/baseMax/displayMax` values on the live divide-by-zero path.

## 2026-06-03
- changed: added a narrow read-only divide-site trace for the two `scene_draw_status_panels()` ratio calculations. `src/main.c` now logs `trace_status_bar_divide_site` at `0x0101477E` and `0x010147AE`, capturing `current/baseMax/displayMax` for the primary and secondary HUD bars before the signed divide helper runs.
- verified: the latest rerun is the first one that truly hit the corrected alternate login mock path. `bin/logs/net_trace.log` now shows `mock_login_alt12_success_response top=1,1,12 len=23` and `queue_login_followup_event ... label=title-role-stage4-after-main`, so the WT-header stage-selection fix is confirmed live.
- verified: even on the now-correct `1/1/12` path, the client still does not stop at a role-list screen. The active title screen remains `010534b4` while it consumes both the minimal `1/1/12 result=1` shell and the staged `1/1/16 -> 1/1/4` follow-up, then later still transitions into the normal post-login scene chain.
- verified: the active blocker has changed again. This rerun no longer dies on `tile26/UI2.gif`; title-side runtime at `tick=173` now performs real scene-piclib loads for `tile25/UI1.gif`, `tile26/UI2.gif`, and `tile29/UI6.gif` on `activeScreen=010534b4`, and later `scene_runtime_init_and_sync()` sees `25/26/29` already populated. The new stdout crash is `Arithmetic exception: Divide By Zero` with `pc=0x0104E980`, `lr=0x01014785`, i.e. the first segmented-bar divide path inside `scene_draw_status_panels()`.
- evidence: `bin/logs/net_trace.log` latest session lines around `6506`, `6515`, `6728-6743`, and `7600+`; `bin/logs/stdout_trace.log` latest session tail with `Divide By Zero`; `scene_draw_status_panels()` divide sites at `0x0101477E` / `0x010147AE`.
- next: rerun once with the new `trace_status_bar_divide_site` log. The key question is which denominator is now zero on the live path: primary `current+0xBC` / meter `+0xC4`, or secondary `current+0xC0` / meter `+0xC8`.

## 2026-06-03
- changed: fixed a WT-header stage-selection bug in the built-in login mock. The alternate `1/1/12` success shell and the staged role-list follow-up ordering now read `kind/subtype` from WT header bytes `request[5]/request[6]`, instead of the previously misused `request[6]/request[7]`.
- verified: the newest runtime logs already showed the live login request as `send_payload ... 01010c00 ...`, but the mock still logged `queue_login_followup_event ... label=title-role-stage4` and never logged `mock_login_alt12_success_response`. Comparing that evidence with the old code showed the branch was accidentally reading the same header as `kind=12, subtype=0`, so the `subtype==12` alternate path never actually fired.
- evidence: `bin/logs/net_trace.log` latest session login request `01010c00`, absence of `mock_login_alt12_success_response`, and the old `src/main.c` uses of `vm_net_mock_get_first_object_kind_subtype()` in `vm_net_mock_build_response()` / `vm_net_mock_on_send()`.
- next: rerun once and confirm the live path now shows both `mock_login_alt12_success_response` and `queue_login_followup_event ... label=title-role-stage4-after-main` before judging the alternate title-flow behavior.

## 2026-06-03
- changed: adjusted the built-in login mock to distinguish the live `1/1/12` login request path from the older `1/1/6 actorinfo` path. Default staged-rolelist mode now answers a `1/1/12` request with a minimal `1/1/12 result=1` success object instead of the previous `1/1/6 actorinfo` object, and the staged `title-role-stage4` follow-up is now queued after that main success response for this alternate path.
- verified: recent runtime logs show the account-confirm request itself is `send_payload ... 01010c00 ... coreVer/appVer/username/password/imsi`, i.e. top-level `1/1/12`. The previous staged experiment was therefore feeding a `1/1/16 -> 1/1/4` pair into the active title screen while the current live request/response stage was still the alternate login path.
- evidence: `bin/logs/net_trace.log` lines around `send_payload len=86 ... 01010c00 ...`, the active `dispatch=05017509` on `screen=010534b4`, and the newly modified builders in `src/main.c`.
- next: rerun with the new `1/1/12` success shell and watch whether the active title screen remains `010534b4`, switches to a different title screen before `title-role-stage4`, or finally starts consuming role-list-specific packets on the alternate success path.
- verified: the new `screen_func_table` trace shows the staged `title-role-stage4` packet is being delivered while the active title screen is still `010534b4`, and that screen does not switch into a different title/UI mode when the `1/1/16 -> 1/1/4` pair arrives. Runtime around the login tap now cleanly shows: `fire_event slot=0` (len `0x55`, staged role packet) leaves `sceneGate=1, sceneState=0` unchanged on `activeScreen=010534b4`, then `fire_event slot=1` (len `0x1B0`, actor-success login packet) flips `sceneGate` and tears down `010534b4`, after which the flow resumes through `01056204 -> 0105a814 -> 01053450` and reaches fresh-enter scene again.
- verified: this means the current staged `1/1/16 -> 1/1/4` experiment is not reaching the earlier assumed role-list response owner in time, or not reaching that owner at all. On the live path, the active post-login title screen before direct enter is the screen whose runtime function table logs as `init=05017621 destroy=050174b1 logic=05017411 render=0501716f pause=05017075 resume=05017025`; the later scene-entry screen logs separately as `init=050679dd destroy=05066b9d logic=05066aff render=05066a97 pause=05066a59 resume=050669ed`.
- evidence: `bin/logs/net_trace.log` lines around `screen_func_table screen=010534b4`, `mock_title_role_stage4_response`, `queue_login_followup_event`, `fire_event slot=0`, `fire_event slot=1`, and the subsequent `screen_manager idx=6 remove ... 010534b4`; `bin/logs/stdout_trace.log` still ends at the older `pc=0x0100E67C` `tile26` draw crash, confirming the role-list experiment itself no longer introduces a separate earlier crash in this run.
- next: stop treating `sub_3544()` as if it were necessarily the active login screen's current data-event callback. The next narrow task is to identify the runtime callback behind `dispatch=05017509` / screen `010534b4`, determine which `type=1/*` objects it actually consumes, and only then adjust the mock sequence toward the real role-list transition owner.
- verified: reordering the staged experiment to queue the title role-list follow-up before the main login-success packet still does not hold the client on the role-list UI. The follow-up now logs as `mock_title_role_stage4_response result16=1 roles=1 len=85`, but the live path still reaches the normal post-login game chain (`type=1/2/3`), then fresh-enter scene, and finally falls back to the older local scene crash at `tile26`.
- verified: this means the active blocker has moved again. The staged title-side packets are no longer obviously malformed or too-late-only, but they still do not switch the current title screen into role-list mode before the generic success continuation takes over. The newest stdout tail no longer stops at `pc=0x010135D4`; instead the run reaches `scene_draw_status_panels()` and crashes again at the known `draw_map_tile_by_index(tileId=26)` null-image fault (`pc=0x0100E67C`, `lr=0x0100E8E3`).
- evidence: `bin/logs/net_trace.log` shows `mock_title_role_stage4_response result16=1 ...`, the built-in `type=1/2/3` requests, fresh-enter `0x1E/1/3/7` handling, and the later `trace_draw_map_tile_entry ... tileId=26`; `bin/logs/stdout_trace.log` ends at the old `0x0100E67C` crash again.
- next: stop focusing on event ordering alone. The next narrow title-side question is which callback or state write actually sets `titleState` / `*(r9+10748)` to `1` (the mode that routes input into `sub_4016()`), because merely delivering the staged `1/1/16 -> 1/1/4` packets is not sufficient on the current path.
- changed: switched the built-in login mock away from the previous same-packet `actorinfo + rolelist` experiment and onto a staged role-list experiment. The primary login reply now defaults back to the normal actor-success object, and `vm_net_mock_on_send()` immediately queues a second `event=7` follow-up packet built by `vm_net_mock_build_title_role_stage4_response()`.
- changed: added a dedicated title-side role-list payload builder in `src/main.c`. `vm_net_mock_build_title_role_list_actorinfo()` now emits the compact counted `actorinfo` table that matches `mmTitleMstarWqvga.cbm sub_3544()` (`count, then repeated u32/u8/u8/string/u32`), and the staged follow-up now sends both `major=1, kind=1, subtype=16 result=1` and `major=1, kind=1, subtype=4 actorinfo=...` in one WT packet.
- verified: the staged experiment is a better fit for the newest static evidence than the old same-packet object-order permutations. `sub_3544()` is the first confirmed title handler that treats `actorinfo` as a role-entry list, so the new mock is now trying to trigger that handler directly instead of only stuffing `roleinfo` or mixed `actorinfo` objects into the initial login-success packet.
- verified: `sub_3544()` is not a plain standalone `subtype=4` parser. Static decomp shows it first consumes `subtype=16` and caches `result` into a local title-state byte; only if that cached byte is `1` does the later `subtype=4` branch parse `actorinfo` and call `sub_247A()`. This makes the previous one-object stage-4 follow-up incomplete.
- evidence: `src/main.c` around `vm_net_mock_build_title_role_list_actorinfo()`, `vm_net_mock_build_title_role_stage4_response()`, `vm_net_mock_login_mode_queues_stage4_role_list()`, and `vm_net_mock_on_send()`; successful `make` rebuild on the modified branch.
- next: rerun one clean login and watch for `queue_login_followup_event ... label=title-role-stage4` plus `mock_title_role_stage4_response result16=1` in `bin/logs/net_trace.log`. The key outcome is whether the new `16 -> 4` pair finally diverts the title flow into character-list state before the old direct-enter chain fully takes over.
- verified: the best current static explanation is no longer "some `actorinfo` field inside login success toggles auto-enter". In `mmTitleMstarWqvga.cbm`, `sub_2D80()` handles the initial `type=1, subcmd=1` login-success wrapper, but there is also a separate `type=1, subcmd=4` handler `sub_3544()` that reads `result`, optional `servconf`, and a counted `actorinfo` list, then calls `sub_247A()` into the generic next-step callback. This makes the outer response stage/wrapper look like the real role-list discriminator.
- verified: the overloaded field name is now a concrete source of confusion. Title-side `sub_3544().actorinfo` behaves like a role-list payload with multiple entries, while main-CBE `parse_actorinfo_response()` still expects the later actor/player blob used by the direct-enter path. So current same-packet experiments around `subcmd=1` are very likely testing the wrong layer.
- changed: switched the built-in login mock's default combined branch in `src/main.c` from `rolelist + actorinfo` to `actorinfo + rolelist`. `vm_net_mock_build_login_response()` still defaults to the mixed two-object experiment, but now serializes the actor-success object first and the role-list object second so we can test whether the later role-list handler can override the earlier auto-enter tendency.
- verified: current evidence is strong enough to treat immediate login `actorinfo` as the wrong default for a real role-list flow. In the title module, role-list confirm handling goes through `sub_4016() -> sub_3E66()`, and `sub_3E66()` builds a later request containing the literal field name `actorinfo`. That makes `actorinfo` look like a post-selection submit, not a field that should be preemptively injected into the initial login-success reply.
- verified: the first rolelist-only retry was too aggressive. Logs showed the client stayed on the login form and repeated the raw login request after each confirm tap, which means the packet still needs a normal login-success shell in addition to the role-list data.
- verified: the follow-up `login-success + rolelist` retry without `actorinfo` is also invalid for the current title parser. The new crash lands at `pc=0x01033A68` (`stream_read_i32_be_tagged_impl`) with `lr=0x0100FAC5` inside `parse_actorinfo_response()`, which is the strong runtime confirmation that the login-success object is still expected to carry an `actorinfo` blob on this path.
- verified: the previous `rolelist + actorinfo` order still auto-entered scene. The latest clean rerun shows `mock_login_role_list_response mode=rolelist-plus-actorinfo ... len=542`, immediately followed by outgoing `type=1`, `type=2`, and `type=3` requests and then the familiar fresh-enter scene chain. So simply adding `roleinfo` to the same login packet is not sufficient when the actor-success object is processed later in that packet.
- verified: the current actorinfo-first mock is what short-circuits the role-list branch on this repository's live path. Existing `bin/logs/net_trace.log` shows `mock_login_actor_response ...` immediately followed by outgoing `type=1`, `type=2`, and `type=3` packets, with no intervening `0x0A/0x20` role-list request before the client moves into fresh-enter scene work.
- evidence: `src/main.c` login builders around `vm_net_mock_build_login_response()`; `mmTitleMstarWqvga.cbm` static RE for `sub_2D80()` (`0x2D80`), `sub_3544()` (`0x3544`), `sub_4016()` (`0x4016`), and `sub_3E66()` (`0x3E66`); `bin/logs/net_trace.log` lines around `mock_login_actor_response` and the immediate `send_payload ... type=1/2/3`.
- next: stop treating "role list versus direct enter" primarily as an `actorinfo` field-layout question. The next protocol experiment should try a real separate `type=1, subcmd=4` role-list response stage after the normal `type=1, subcmd=1` login success, instead of only permuting same-packet object order inside the initial success reply.

## 2026-06-03
- changed: translated the startup/update screen init path into human-readable names by renaming `0x0103B0B0 -> startup_screen_update_init`, `0x0103B1DE -> startup_screen_update_bind_callbacks`, and the small `loading.gif` helper chain `0x010461D0 -> loading_gif_widget_init`, `0x010461A8 -> loading_gif_widget_draw`, `0x01046048 -> loading_gif_widget_draw_frame`, plus `0x01018660 -> scene_get_loading_gif_widget`.
- verified: `startup_screen_update_init()` is not part of the scene tile catalog path. It initializes the startup/update screen's own image owner, loads `UI2.gif`, `UI8.gif`, `UI7.gif`, and `flowerStyle.gif` into that private owner, then constructs an embedded `loading.gif` widget and seeds the startup/update state bytes before rendering begins. This confirms the old `UI2.gif` confusion: this path is screen-local startup art and does not populate `sceneUiTileCatalog` tile 26.
- verified: `loading_gif_widget_init()` cleanly reads as a tiny reusable widget constructor. Depending on a flag, it either reuses the shared image-owner path or opens `loading.gif` directly from the supplied resource root, stores the image slot id, and installs `loading_gif_widget_draw()` as the widget callback. `loading_gif_widget_draw()` then toggles animation state off the global loading flag and delegates to `loading_gif_widget_draw_frame()`.
- evidence: `startup_screen_update_init()` decomp/disasm at `0x0103B0B0`; callback-table wrapper at `0x0103B1DE`; widget init/draw helpers at `0x010461D0`, `0x010461A8`, and `0x01046048`; scene-side accessor at `0x01018660`.
- next: if this startup/loading chain needs more polish later, the next natural target is the surrounding startup/update logic callbacks (`sub_103ADA4`, `sub_103AE4A`) so the whole startup screen lifecycle can be read end-to-end without anonymous handlers.

## 2026-06-04
- changed: continued the `mmTitleMstarWqvga.cbm` post-login trace and renamed the title-local role flow into `title_register_local_screens`, `role_list_screen_init/handle_action/handle_touch/render`, `title_handle_role_list_response`, `role_manage_screen_init/dispatch_mode_input/handle_input`, and `role_manage_submit_selected_role`. Also clarified the router helper as `title_transition_router_invoke_default_target`.
- verified: the values previously seen in the title login-success chain (`0x2844`, `0x28CC`, `0x203C`, `0x28E4`, `0x2B30`) are not code entrypoints; they are local screen/state object offsets registered by `title_register_local_screens()`. The object mapping is now stable: `r9+0x28CC` = first local screen (`title_four_choice_screen_*`), `r9+0x2844` = `login_form_screen`, `r9+0x203C` = `role_list_screen`, `r9+0x28E4` = later `role_manage_screen`, and `r9+0x2B30` = the remaining login/recharge variant screen.
- verified: the immediate primary-login handoff target is the third local screen, not main-CBE scene code. `login_response_result_dispatch('1')` calls the title transition router object at `r9+0x28CC` with the stage-4 target loaded from `r9+0x2A34`; `title_register_local_screens()` shows that slot corresponds to the `role_list_screen` object at `r9+0x203C`.
- verified: `role_list_screen_render()` proves this intermediate screen is a real local workflow stage. It has a loading branch and, after two ticks, sends `1/1/16`; its paired network handler `title_handle_role_list_response()` first caches subtype `16` result, then parses subtype `4` counted `actorinfo/roleinfo` into the same local buffers consumed by the render path, and finally re-invokes the title transition router.
- evidence: static analysis of `login_response_result_dispatch()` (`0x23C0`), `login_stage_success_dispatch()` (`0x1956`), `title_register_local_screens()` (`0x5864`), `role_list_screen_render()` (`0x31D4`), and `title_handle_role_list_response()` (`0x3544`) in `mmTitleMstarWqvga.cbm`.
- next: if we need the full post-role-list handoff, the next narrow target is to prove whether `title_handle_role_list_response()`'s final router callback advances into the registered `role_manage_screen` object at `r9+0x28E4`, or whether there is one more local target indirection in between.

## 2026-06-04
- changed: tightened the title-local screen progression by renaming the later role-management helpers into `role_manage_screen_render`, `role_manage_screen_handle_network`, `role_manage_screen_handle_create_role_result`, `role_manage_screen_remove_role_slot`, `role_manage_screen_select_role_slot`, and `role_manage_screen_sync_selection_highlight`, then annotating the selector-3 path in `title_four_choice_screen_handle_action()`.
- verified: the strongest current static explanation for the post-role-list continuation is now narrower. In `title_four_choice_screen_handle_action()` (`0x2724`), selector `3` does **not** directly jump to `role_manage_screen`; instead it calls the `role_list_screen` object's method at offset `+0x18` with `r9+0x28E4` (the registered `role_manage_screen` object) as the argument, then switches local state. Later, `title_handle_role_list_response()` (`0x3544`) completes subtype `16/4` parsing and finishes with `title_transition_router_invoke_default_target()`. Together these two facts strongly suggest `role_list_screen` is first configured with a “completion target” of `role_manage_screen`, and the final no-arg router callback consumes that prepared target.
- evidence: `title_four_choice_screen_handle_action()` disassembly at `0x278E..0x279C`; `title_register_local_screens()` object-slot map at `0x5864..0x5914`; `title_handle_role_list_response()` tail call at `0x36D2..0x36D8`.
- next: this is now a high-confidence hypothesis rather than a proven runtime observation. The most direct confirmation would be a trace or memory watch on the `role_list_screen` object around its `+0x18` method, or a title-side callback trace showing the default router handoff landing on `role_manage_screen` after subtype `4` finishes.

## 2026-06-04
- changed: continued refining the `role_list_screen +0x18` path and renamed two activator helpers into `title_activate_role_manage_screen` and `title_activate_popup_screen_if_idle`.
- verified: the `role_list_screen` object's `+0x18` slot now has a clearer shared meaning. It is called with `r9+0x28E4` (`role_manage_screen`) in `title_activate_role_manage_screen()` and selector-3 setup, and it is also called with `r9+0x29CC` (the popup/local overlay screen object) in `title_activate_popup_screen_if_idle()`. This strongly argues that `+0x18` is not a role business handler but a local-screen target setter/activator used to attach another title-local screen/overlay object to the role-list flow.
- verified: this strengthens the earlier continuation hypothesis: primary login success enters `role_list_screen`, and later progression to `role_manage_screen` likely happens by consuming the target previously installed through that same `+0x18` hook before `title_transition_router_invoke_default_target()` is re-entered.
- evidence: `title_activate_role_manage_screen()` (`0x54A`) disassembly at `0x54A..0x55A`; `title_activate_popup_screen_if_idle()` (`0xFAA`) disassembly at `0xFCE..0xFDC`; selector-3 setup in `title_four_choice_screen_handle_action()` at `0x278E..0x279C`.
- next: remaining ambiguity is whether `+0x18` means “set next default target”, “activate child screen now”, or a hybrid queue/stack operation. A runtime trace on this slot would settle that last semantic difference quickly.

## 2026-06-03
- changed: continued partitioning Batch 5 draw consumers by renaming `0x01014168 -> scene_draw_team_member_status_list`, `0x01013A7A -> scene_draw_concern_strip`, `0x01036246 -> scene_prompt_grid_render`, `0x010360DA -> scene_prompt_grid_draw_rows`, and `0x0104625C -> scene_draw_minimap_overlay`, then tying their tile ids back to the preload table.
- verified: Batch 5 now has concrete scene-runtime consumers beyond the EXP overlay. `scene_draw_team_member_status_list()` consumes `tile76=leader.gif` and `tile77=ui_offline.gif` on member rows, with `tile71=handkuang` and `tile70=tiao` providing the row chrome. `scene_draw_concern_strip()` consumes `tile78=concernp.gif` and `tile69=concernf1.gif` over a `UI7.gif` strip base. `scene_draw_minimap_overlay()` consumes `tile79=wm_hero.gif` as the local-player minimap marker while separately plotting active scene nodes and movement/path points.
- verified: the older “map frame” part of Batch 5 is now also grounded. `scene_prompt_grid_draw_rows()` uses `tile20=map_kuang2.gif` for the selected row frame and `tile21=map_kuang.gif` for normal rows inside the scene prompt-grid panel. The prompt-grid render wrapper at `scene_prompt_grid_render()` calls that row pass directly. Separately, `task_hall_screen_tick()` draws `tile75=FbImage.gif` as a task-hall entry badge/icon.
- evidence: `scene_draw_team_member_status_list()` at `0x01014168`; `scene_draw_concern_strip()` at `0x01013A7A`; `scene_runtime_tick()` callsite at `0x01015228`; `scene_draw_actor_pass()` callsite at `0x01014652`; `scene_prompt_grid_draw_rows()` at `0x010360DA`; `scene_prompt_grid_render()` at `0x01036246`; `scene_draw_minimap_overlay()` at `0x0104625C`; `task_hall_screen_tick()` drawsite comment at `0x0104AF6C`.
- next: the remaining useful cleanup on this tile chain is to keep converting the still-generic consumers around `sub_1019DFA`/`sub_1019B72` and related overlay helpers into named badge/button renderers, so every persistent scene/runtime tile family from the preload table has a matching business-level draw helper.

## 2026-06-03
- changed: kept cleaning the `scene_ui_tile_catalog_preload_runtime_tiles()` consumers by renaming `0x010138BC` to `scene_draw_exp_progress_overlay` and `0x01045578` to `scene_draw_node_overhead_overlay`, then tying their tile use back to Batch 5 in IDA comments.
- verified: Batch 5 is no longer just a preload list. `scene_draw_exp_progress_overlay()` is now the concrete consumer for the EXP subset: `tile42=uiexp`, `tile72=numberexp`, `tile71=handkuang`, and `tile13/14=exp0/exp1`. `scene_runtime_tick()` calls this path at `0x010156A2`, so the preload-to-draw chain for that group is now explicit.
- verified: the node-title / overhead-marker path is also clearer. `scene_draw_node_overhead_overlay()` uses the shared actor-asset pipeline to resolve optional per-node overhead badges/icons instead of hardcoding those assets through the tile catalog, while the nearby Batch 5 concern/marker tiles remain part of the broader scene-overlay family.
- evidence: `scene_draw_exp_progress_overlay()` at `0x010138BC`; `scene_runtime_tick()` callsite at `0x010156A2`; `scene_draw_node_overhead_overlay()` at `0x01045578`, especially the actor-asset resolution path around `0x0104569A..0x010456E0`.
- next: if this cleanup continues, the next good target is to identify which concrete drawsites consume the remaining Batch 5 map/frame/leader/offline tiles, so the whole preload list can be partitioned into named overlay families instead of just “misc scene decorations”.

## 2026-06-03
- changed: continued the `0x01045EA4 scene_ui_tile_catalog_preload_runtime_tiles()` cleanup by renaming its loop locals to `statusStripTileOffset`, `meterBarTileOffset`, and `hudGlyphTileId`, and by splitting the long `loadTile()` sequence into explicit preload batches in IDA comments.
- verified: the function now reads cleanly as five resource groups instead of one anonymous magic-number list: Batch 1 preloads core HUD numerics/chrome (`time_number`, hp/sp numbers, menu strip, status markers, training/vip/ruffian/ep icon pieces); Batch 2 preloads `tile59..65 = UIsh1..UIsh7` for the fixed horizontal status strip; Batch 3 preloads `tile46..49 = UIxia1..UIxia4` for the two segmented meter bars; Batch 4 preloads `tile0..7` base digit glyphs; Batch 5 preloads the remaining scene overlay decorations such as `tiao`, `handkuang`, `uiexp`, `numberexp`, `map_kuang*`, `FbImage`, `leader`, and `ui_offline`.
- verified: `scene_ui_tile_catalog_build_filename_table()` is now aligned with that explanation in IDA, so the preload helper can be read directly against the fixed tile-id -> GIF-name namespace instead of guessing from raw tile numbers.
- evidence: `scene_ui_tile_catalog_preload_runtime_tiles()` at `0x01045EA4`, especially comment groups at `0x01045EAC`, `0x01045F44`, `0x01045F5E`, `0x01045F72`, and `0x01045F84`; filename table at `scene_ui_tile_catalog_build_filename_table()` (`0x010434CE`).
- next: the next useful cleanup on this path is to keep tying concrete draw sites back to these batch names, especially the exp/map/overlay tiles in Batch 5 that still appear in scene runtime code as raw tile ids.

## 2026-06-03
- changed: continued the `R9+0x5C48` cleanup by promoting the scene-object getters at `0x01018042/0x01018050/0x01018058` into `scene_get_actor_asset_slot_table()`, `scene_get_hud_actor_asset_owner()`, and `scene_get_actor_asset_ops_descriptor()`, then annotated the main use sites in `scene_runtime_init_and_sync()`, `scene_hud_main_panel_init()`, `sub_1045578()`, and `scene_actor_asset_slot_table_load_entry()`.
- verified: `R9+0x5C48` is broader than just “entry-init + short-id lookup”. Static xrefs now show at least five live slots in the shared descriptor: `+0x00` resolves a named HUD/actor asset through the shared owner/context and writes back a short resource id; `+0x08` initializes one 44-byte actor-asset entry from `(assetOwnerCtx, assetName)`; `+0x0C` resolves the short resource id for an actor asset name; `+0x10` performs a generic HUD panel base initialization used by `scene_hud_main_panel_init()`; and `+0x14` is called at the top of `scene_runtime_init_and_sync()` to prime or validate the shared HUD actor-asset runtime state before portrait/helper widget binding.
- verified: the scene object now clearly exposes this mini-subsystem through three neighboring vtable slots: slot `20` returns the reusable actor-asset slot table at `R9+0x5FD0`, slot `28` returns the shared HUD actor-asset owner/context at `R9+0x5F5C`, and slot `32` returns the shared ops descriptor at `R9+0x5C48`.
- evidence: `scene_object_vtable_init()` assignments at `0x01019350/0x0101935C/0x01019362`; `scene_runtime_init_and_sync()` pre-bind call at `0x0101305C`; `scene_hud_main_panel_init()` call through `R9+23640` at `0x010352D4`; `sub_1045578()` named-asset path at `0x0104569A..0x010456E0`; `scene_actor_asset_slot_table_load_entry()` callback uses at `0x01044DBE` and `0x01044EDC`.
- next: the remaining open question is still the constructor for the inline descriptor itself. No direct initializer/store site for `R9+0x5C48` was confirmed in this pass, so the next useful experiment is to keep tracing who seeds that shared ops block before `scene_runtime_init_and_sync()` first calls slot `+0x14`.

## 2026-06-03
- changed: tightened the relationship between the inline `R9+0x5C48` descriptor and the actor-asset slot-table helpers, and renamed the helper family from the earlier generic “named asset table” wording to the more accurate `scene_actor_asset_slot_table_*`.
- verified: `R9+0x5C48` is not just a portrait-widget-only black box. Static decomp of `scene_actor_asset_slot_table_load_entry()` shows that the same global callbacks at `R9+0x5C50` and `R9+0x5C54` are used by the generic actor-asset slot-table path: `+0x08` initializes a 44-byte actor-asset entry from `(assetOwnerCtx, assetName)`, and `+0x0C` resolves/looks up the short id for a resource name. The portrait path in `scene_runtime_init_and_sync()` simply calls the shared entry-init callback directly on the portrait widget entry, bypassing the slot-table wrapper.
- verified: this clarifies the split cleanly. Portrait widgets and helper widgets still share the same `sceneHudActorAssetOwner` context at `R9+0x5F5C`, but they differ only in how they reach the shared actor-asset ops: portrait widgets call the entry-init callback directly, while helper widgets go through `scene_actor_asset_slot_table_bind_entry()` / `scene_actor_asset_slot_table_load_entry()`.
- evidence: `scene_actor_asset_slot_table_load_entry()` decomp at `0x01044DA0` now shows the direct uses of `v3 + 23632` and `v3 + 23636` (that is, `R9+0x5C50` and `R9+0x5C54`); `scene_runtime_init_and_sync()` portrait path at `0x010136A8..0x010136B0`; helper path at `0x01013706..0x0101376A`.
- next: the remaining open question is no longer “are these two different systems?”, but “where is the shared actor-asset ops descriptor at `R9+0x5C48` initialized?”. Finding that initializer should finally reveal concrete names for the `+0x08/+0x0C` callbacks.

## 2026-06-03
- changed: continued the scene HUD actor-resource cleanup around `0x0101366A..0x01013770` by making the shared owner/context and the portrait-specific binder path explicit in IDA comments and helper signatures.
- verified: `R9+0x5F5C` is a shared scene HUD actor-asset owner/context reused by both the portrait-widget path and the helper/marker actor-widget path. The generic helper/marker widgets call `scene_actor_asset_slot_table_bind_entry()` with this owner/context, while the portrait widgets use a distinct callback loaded from `R9+0x5C48+8` that understands the 44-byte portrait-widget layout.
- verified: the portrait and helper paths now read as separate flows. Portrait widgets: reset 44-byte entry -> `build_actor_resource_name(slot, 1, ...)` -> specialized portrait-widget binder with `sceneHudActorAssetOwner`. Helper widgets: reset 44-byte entry -> bind `fuhao.actor` / `b_tele.actor` / `chuansong.actor` through the generic named-asset table with the same owner/context.
- verified: repository-side evidence now matches the static RE split. `src/main.c` already carries two separate actor-name tables: `vm_net_mock_actor_resource_name()` returns `h_* / hW_*` scene actor resources, and `vm_net_mock_actor_ui_motion_name()` returns `ui_h_* / ui_hw_*` portrait/UI actor resources. This aligns with `build_actor_resource_name(slot, 0, ...)` versus `build_actor_resource_name(slot, 1, ...)` in the scene code.
- evidence: `scene_runtime_init_and_sync()` disassembly/comments at `0x010136A8`, `0x010136AC`, `0x010136B0`, `0x01013706`, `0x01013736`, and `0x0101376A`; helper signatures and comments for `scene_actor_asset_slot_table_load_entry()` / `scene_actor_asset_slot_table_bind_entry()`.
- evidence: `src/main.c` around `vm_net_mock_actor_ui_motion_name()` and `vm_net_mock_actor_resource_name()`; `build_actor_resource_name()` callsites at `0x01013696` and `0x0100F7FC`.
- next: the best next target is still to locate who initializes the inline portrait-widget loader descriptor around `R9+0x5C48`, so the current business-level comment can be promoted into a concrete class/method name.

## 2026-06-03
- changed: refined the `scene_runtime_init_and_sync()` / `scene_rebuild_runtime_nodes()` actor-widget understanding and renamed the old `scene_prompt_entry_table_*` helpers to the broader `scene_actor_asset_slot_table_*` family.
- verified: the two 6-entry loops serve different objects. In `scene_rebuild_runtime_nodes()`, `build_actor_resource_name(slot, 0, ...)` feeds the six core actor-resource slots on `sceneCurrentNode` itself; this is not the portrait-widget family. Later in `scene_runtime_init_and_sync()`, the `0x0101366A` loop builds the six 44-byte portrait actor widgets, using `build_actor_resource_name(slot, 1, ...)` for the UI portrait actor resources (`ui_h_*` / `ui_hw_*` style assets).
- verified: the seven `sceneHudHelperActorWidgets` are initialized separately. The `0x010136E2` loop resets each 44-byte helper widget and binds `fuhao.actor`, then two standalone helper widgets are reset and bound to `b_tele.actor` and `chuansong.actor`. This makes the helper family clearly distinct from both the `sceneCurrentNode` actor slots and the portrait actor widgets.
- verified: the table at `globalR9+0x5FD0` is better described as a reusable named-asset slot table than a prompt-only structure. Its `load/bind` callbacks are reused by scene actor/widget setup to resolve asset names and patch caller-owned output references, with deferred completion support when the asset is not immediately available.
- evidence: `scene_rebuild_runtime_nodes()` disassembly at `0x0100F7EA..0x0100F81A`; `scene_runtime_init_and_sync()` disassembly at `0x0101366A..0x01013770`; `build_actor_resource_name()` callers at `0x0100F7FC` and `0x01013696`; renamed helpers `scene_named_asset_table_init()`, `scene_actor_asset_slot_table_load_entry()`, and `scene_actor_asset_slot_table_bind_entry()`.
- next: the next clean-up target is the unknown owner/context at `R9+0x5F5C` and the portrait-widget loader path at `[R9+0x5C48+8]`, which should let us promote the current “portrait actor widget” comments into concrete class/method names.

## 2026-06-03
- changed: renamed `0x01045EA4` from the generic `scene_preload_ui_tile_slots()` into `scene_ui_tile_catalog_preload_runtime_tiles()` and tightened its signature to `struct SceneUiTileCatalog *`. Added grouped comments for the tile batches it preloads and updated the `scene_runtime_init_and_sync()` call-site note accordingly.
- verified: this helper is broader than a status-bar-only preload. It operates on the shared `sceneUiTileCatalog` owner and bulk-loads the fixed scene-runtime tile set: status-strip `UIsh1..7`, segmented-bar `UIxia1..4`, base digit tiles `0..7`, plus prompt/map/exp/fb/leader/offline decorations such as `handkuang.gif`, `tiao.gif`, `map_kuang*.gif`, `FbImage.gif`, `leader.gif`, and `ui_offline.gif`. It is therefore a catalog-level scene-runtime UI preload helper, not a single widget image initializer.
- verified: the closest same-family functions are the `sceneUiTileCatalog` methods installed by `scene_ui_tile_catalog_init()` (`loadTile`, `drawTileRegion`, `drawTileExt`, `drawTileAt`, `drawTileClipped`, `destroy`), not another paired preload routine. A nearby separate helper family at `0x010461D0/0x01046048/0x010461A8` uses `loading.gif` and a small state object, which confirms that `scene_ui_tile_catalog_preload_runtime_tiles()` belongs to the shared tile-catalog path rather than the standalone loading/progress widget path.
- evidence: `scene_runtime_init_and_sync()` call at `0x010137B6`; `scene_ui_tile_catalog_build_filename_table()` file map at `0x010434CE`; catalog method installation in `scene_ui_tile_catalog_init()` at `0x01043BD4..0x01043BF8`; `loading.gif` widget helpers at `0x010461D0`, `0x010461A8`, and `0x01046048`.
- next: if we continue this catalog path, the next high-value cleanup is to give the individual scene HUD widgets more concrete class names and then tie each draw site to the exact preloaded tile group it consumes.

## 2026-06-03
- changed: continued cleaning `scene_runtime_init_and_sync()` by separating the small popup/message HUD objects from the larger prompt-grid controller. Added/updated `GlobalR9Context2` fields such as `sceneHudAuxPanel`, `sceneHudPromptMessageWindow`, `sceneHudHelperActorWidgets`, `sceneHudPromptEntryTable`, and `sceneHudPromptGridPanel`, and renamed helper initializers like `attach_data_sheet_loader_methods()` and `scene_prompt_entry_table_init()`.
- verified: the key `R9+0x5C64` scene-HUD slots now read as distinct object families instead of one anonymous blob. `+0x40` is the active `sceneCurrentNode`, `+0x48` is the derived `sceneStatusMeterNode`, `+0x5C` is the selected `sceneHudCurrentPortraitWidget`, and `+0x78` is the centered `sceneHudStatusText`. In the init flow, `0x01013122` allocates six 44-byte portrait/status widgets, `0x01013138` allocates the main 0x694-byte HUD panel, `0x01013142` allocates a paired auxiliary 0x694-byte HUD panel, `0x0101314E` allocates the smaller 0x378 prompt/message window, and `0x0101315A` allocates seven 44-byte helper actor widgets for the HUD side strip. The later `0x0100F90A` init call targets the separate `sceneHudPromptGridPanel` at `globalR9+0x9540`, not the small popup/message window.
- verified: the `0x01013780..0x010137B6` phase can now be read as “enable/dirty the HUD, publish `sceneCurrentNode` into the scene controller, then preload fixed HUD tile slots only after portrait widgets, helper actor widgets, and the main HUD panel are already bound.” This is the transition from object binding to resource preloading, not another semantic state rebuild.
- evidence: `scene_runtime_init_and_sync()` allocation sites at `0x01013122/0x01013138/0x01013142/0x0101314E/0x0101315A`; `scene_rebuild_runtime_nodes()` calls at `0x0100F8F6` and `0x0100F90A`; `attach_data_sheet_loader_methods()` xref at `0x010131AE`; `scene_prompt_entry_table_init()` xref at `0x010131BC`; enable/preload path at `0x01013780..0x010137B6`.
- next: if we continue this line, the best follow-up is still to identify the concrete constructors and draw callbacks for the six 44-byte `sceneHudPortraitWidgets` and the seven 44-byte `sceneHudHelperActorWidgets`, so those two widget families can be promoted from “role-known object groups” to full type-backed UI classes.

## 2026-06-03
- changed: cleaned up the tile/status-strip side of `scene_draw_status_panels()`. The shared `R9+0x9AF0` object is now named `sceneUiTileCatalog`, with helper methods renamed along the `scene_ui_tile_catalog_*` and `scene_ui_draw_tile_*` chain. The selected left portrait widget field was also renamed from `sceneHudCurrentStatusWidget` to `sceneHudCurrentPortraitWidget`.
- verified: `R9+0x5C64+0x5C` is not the fixed UIsh tile strip; it is the currently selected 44-byte portrait/status widget from the six-entry `sceneHudPortraitWidgets` array, chosen earlier in `scene_runtime_init_and_sync()` from `sceneCurrentNode` visual bytes. Its `+0x18` callback at `0x010146DA` is the draw method for the left-side portrait/status block. The fixed tile row at `0x010146F2..0x01014710` instead uses `sceneUiTileCatalog->drawTileAt()` to place `tile59..62` (`UIsh1.gif..UIsh4.gif`) at `x=90/121/152/183, y=39`, then `tile64` (`UIsh6.gif`) at `x=214, y=39`; `tile65` (`UIsh7.gif`) is the full-width `240x35` strip background, and `tile71` (`handkuang.gif`) is the portrait highlight/frame overlay.
- evidence: `scene_system_bootstrap()` allocates/inites `sceneUiTileCatalog` at `0x01003940..0x01003950`; `scene_ui_tile_catalog_init()` installs `load/draw/destroy` helpers and the filename table at `0x01043BCC..0x01043BF8`; `scene_runtime_init_and_sync()` chooses the portrait widget at `0x010136DE`; `scene_draw_status_panels()` calls the portrait-widget draw at `0x010146DA`, the tile ext path for `handkuang.gif` at `0x010146C8`, and the fixed UIsh strip draws at `0x010146F2..0x01014710`.
- next: if we continue this HUD/UI pass, the next high-value cleanup is to identify the concrete 44-byte portrait widget constructor behind `sceneHudPortraitWidgets`, so its `+0x18` draw callback can be named as a real function instead of staying a business-level comment.

## 2026-06-03
- changed: split `ActorSceneNode` gap region `+0x9C..+0xEF` so the six scene-HUD meter fields now have explicit names: `statusBarPrimaryCurrent`, `statusBarSecondaryCurrent`, `statusBarPrimaryBaseMax`, `statusBarSecondaryBaseMax`, `statusBarPrimaryDisplayMax`, and `statusBarSecondaryDisplayMax`.
- verified: the naming matches both the draw path and the growth/rebuild helpers. `scene_draw_status_panels()` reads `sceneCurrentNode->{Primary/SecondaryCurrent}` against `sceneStatusMeterNode->{Primary/SecondaryDisplayMax}`, gated by the corresponding `BaseMax` fields. `scene_apply_levelup_status_growth()` increases both base maxima and display maxima on level-up, then refills current values from the rebuilt meter node.
- evidence: `scene_copy_status_snapshot_node()` now decompiles with the named fields at `0x01045022..0x01045048`; `scene_draw_status_panels()` reads them at `0x01014776..0x010147BC`; `scene_apply_levelup_status_growth()` updates them at `0x01017F5A..0x01017FC4`.
- next: if we keep cleaning `ActorSceneNode`, the next useful step is naming the neighboring `+0x84/+0x86/+0x88...` short fields that are updated together with status growth and likely represent derived combat stats shown elsewhere in the scene UI.

## 2026-06-03
- changed: continued the scene-HUD cleanup by renaming the late tile preloader to `scene_preload_ui_tile_slots()` and the centered status-text helper to `draw_centered_text_rgb()`, then annotated the two segmented-bar calculations inside `scene_draw_status_panels()`.
- verified: the HUD initialization order is now explicit in IDA. `scene_runtime_init_and_sync()` first binds `sceneCurrentNode`, chooses `sceneHudCurrentStatusWidget`, and builds the HUD/prompt objects; only after that does `scene_preload_ui_tile_slots()` request the fixed tile-slot set used by scene HUD chrome. The two segmented bars then read their live values from `sceneCurrentNode` and their derived caps from `sceneStatusMeterNode`.
- evidence: renamed helpers at `0x01045EA4` and `0x010351BA`; line comments at `0x010137B6`, `0x01014722`, `0x01014776`, and `0x010147A6`; decompile/xref path through `scene_runtime_init_and_sync()` and `scene_draw_status_panels()`.
- next: if we keep polishing this chain, the highest-value cleanup is naming the paired status-gauge fields inside `ActorSceneNode` around offsets `+180..+200`, so the two HUD bar calculations stop reading as raw node offsets.

## 2026-06-03
- changed: traced the status-panel dependency chain backward from `scene_draw_status_panels()` and renamed the key `GlobalR9Context2` HUD/state slots around `R9+0x5C64`.
- verified: `R9+0x5C64+0x40` is `sceneCurrentNode`, written in `scene_rebuild_runtime_nodes()` when slot 0 of the 25-node pool becomes the active scene node. `R9+0x5C64+0x48` is a dedicated `sceneStatusMeterNode`, allocated during `scene_system_bootstrap()` and refreshed by `scene_rebuild_status_meter_node()`. `R9+0x5C64+0x5C` is `sceneHudCurrentStatusWidget`, selected in `scene_runtime_init_and_sync()` from the six `sceneHudStatusWidgets` using the current node's visual bytes. `R9+0x5C64+0x78` is `sceneHudStatusText`, written from scene data parsing (`sub_100F094`, field kind `0x16`) and drawn by `scene_draw_status_panels()` at `(120,16)`.
- verified: the companion initialization split is now clearer. `sceneCurrentNode` binding is followed by actor named-resource setup (`build_actor_resource_name()` loop) and helper actor loads (`fuhao.actor`, `b_tele.actor`, `chuansong.actor`), while scene tile-slot preloading happens later at `sub_1045EA4()` after `sceneHudCurrentStatusWidget` is already bound. `sceneStatusMeterNode` rebuild does not preload tiles; it only recomputes node-local meter/effect values and copies the resulting maxima back into the current node.
- evidence: `scene_system_bootstrap()` allocation sites at `0x010038F4/0x01003900`; `scene_rebuild_runtime_nodes()` write at `0x0100F7CA`; `scene_runtime_init_and_sync()` widget selection at `0x010136DE` and tile preload at `0x010137B6`; `scene_rebuild_status_meter_node()` logic around `0x0100FF16..0x01010222`; `scene_draw_status_panels()` reads at `0x010146D2`, `0x01014716`, `0x0101476E`, and `0x0101479A`.
- next: decide whether `sceneTemplateNode` (`R9+0x5C64+0x44`) can now be promoted from a conservative helper-node name into a more specific “player/status source node” role, since it is the remaining adjacent scene-node object that still feeds several meter/HUD paths.

## 2026-06-03
- changed: split the `GlobalR9Context2` gap regions around `0x5CBC..0x5D08` so the scene HUD runtime slots now have explicit field names instead of anonymous `gap_*` coverage.
- verified: the following `R9` offsets are the scene HUD object slots used by `scene_runtime_init_and_sync()` allocation sites: `0x5CBC/23740 -> sceneHudStatusWidgets`, `0x5CC4/23748 -> sceneHudMainPanel`, `0x5CC8/23752 -> sceneHudSecondaryPanel`, `0x5CD4/23764 -> sceneHudPromptWindow`, `0x5D04/23812 -> sceneHudSideBadgeWidgets`.
- evidence: `scene_runtime_init_and_sync()` disassembly at `0x01013122`, `0x01013138`, `0x01013142`, `0x0101314E`, and `0x0101315A` shows direct heap allocations stored into those exact offsets, with sizes matching the previously identified HUD/status-widget objects.
- next: keep following callers that still use adjacent unnamed slots such as `0x5CC0` or `0x5CEC` to decide whether they belong to the same HUD panel family or to neighboring scene-status text/state fields.

## 2026-06-03
- verified: no second same-level status-tile preloader has been found around `sub_1045EA4()` in the current main CBE. A direct xref check still shows only one caller: `scene_runtime_init_and_sync()` at `0x010137B6`. A broader local scan over `0x01045000..0x01065000` also failed to find another function with the same “repeated `piclib->loadTile` via `+0xC` and many immediate tile ids” shape touching `tile26` or the `59..65` status-panel strip range.
- verified: the immediate neighbors around `sub_1045EA4()` are not alternate preload branches for the same status contract. In particular, `sub_1045E0A()` is a generic helper-object/vtable initializer whose callers (`sub_1033262`, `sub_103550E`, `sub_103581E`, plus the scene init setup site at `0x010131AE`) use it for `.dsh`/data-sheet style loader objects (`item.dsh`, `skill.dsh`, `wMap.dsh`, `sMap.dsh`), not for scene tile-family preloading. Nearby `sub_1045B8C()` / `sub_1045C60()` / `sub_1045D28()` are the corresponding object methods for file-backed table loading and cleanup, again unrelated to the status-panel tile contract itself.
- verified: this makes the omission look structural rather than path-selection based inside the current main CBE. The only concrete status-panel tile preloader still visible is `sub_1045EA4()`, and in the current binary it explicitly includes the `UIsh1..UIsh7` strip (`59..65`) while excluding `tile26/UI2.gif`.
- evidence: `xrefs_to(sub_1045EA4)` returning only `0x010137B6`; disassembly of `sub_1045EA4()`; neighbor analysis for `sub_1045E0A()`, `sub_1045B8C()`, `sub_1045C60()`, `sub_1045D28()`, and their callers `sub_1033262()` / `sub_103550E()` / `sub_103581E()`; local `py_eval` scan over `0x01045000..0x01065000`.
- next: if we keep chasing the “tile26 should have been in the same contract” hypothesis, the remaining plausible places are now outside a same-level sibling-preloader in the current main CBE:
  - an older/other binary/module version of the same contract
  - a one-off bridge/fill specifically for `UI2.gif`
  - or a semantic reason the status-panel background was intentionally split out from the `UIsh*.gif` preload block

## 2026-06-03
- verified: the status-panel-specific preload gap is now much stronger statically. `sub_10434CE()` maps tile ids `59..65` to the contiguous filename family `UIsh1.gif` .. `UIsh7.gif`, while `tile26` remains `UI2.gif`. `sub_1045EA4()` then preloads that exact contiguous `59..65` run in one block, but still never requests `26`.
- verified: `scene_draw_status_panels()` consumes those same assets in one local status-bar draw path. It always draws `tile26` first and `tile65` second (`0x010146A4`, `0x010146B8`), then immediately uses the piclib draw method at `+0x20` to draw `tile59`, `60`, `61`, `62`, and finally `64` (`0x010146F2..0x01014710`). So the crashing `UI2.gif` tile and the successfully preloaded `UIsh*.gif` strip tiles are not just “same broad UI family”; they are part of the same concrete status-panel render contract.
- verified: this makes the missing contract narrower than “scene never seeded status UI at all”. The scene-local preloader `sub_1045EA4()` is already seeding the status-panel strip/icon side (`UIsh1..UIsh7`), but not the full-width background tile `UI2.gif` that the same draw function consumes first.
- evidence: `sub_10434CE()` disassembly around `0x010435C0..0x010435DC`; `sub_1045EA4()` disassembly around `0x01045F38..0x01045F4E`; `scene_draw_status_panels()` disassembly around `0x010146A4..0x01014710`.
- next: the strongest remaining static hypothesis is now specific: `tile26/UI2.gif` should have been part of the same status-panel preload contract as `59..65`, but the current live path only runs the `UIsh*.gif` half. The next useful target is therefore the immediate provenance of that omission: either an older/alternate version of the same preloader, or a one-off background-tile fill that was expected to run just before/after `sub_1045EA4()`.

## 2026-06-03
- verified: the newer “UI3/UI4/UI7/UI8/UIsh7 are already up, so UI2 may belong to the same initializer” guess is now weaker after following the actual scene consumers. The scene-side UI consumers split into separate families:
  - `scene_draw_status_panels()` draws tile `26` and `65`, then immediately uses the piclib object to draw tiles `59..62` plus `64`.
  - `sub_1013A7A()` is a different overlay/banner consumer that draws tiles `30/35` and reads width/height from the piclib slots around `tile30/31`.
  - `sub_10489E8() -> sub_1048B14()` and `sub_101A13A()` are popup/list-style consumers that use tiles `27` and `28`.
- verified: that means the most scene-local cluster around the crash is not `{26,27,28,30,31,65}` but `{26,59..65}`. This lines up with the existing `sub_1045EA4()` preload list, which explicitly requests `59..65` and skips `26`. So the current live asymmetry is narrower than “one missing member of a shared UI family initializer”: the scene-local status-panel preloader is already seeding the icon/strip side of the status cluster, but not the `UI2.gif` full-width background tile itself.
- verified: the scene consumers for the other previously observed tiles are now identified and appear unrelated to the crashing status-panel background:
  - `tile30/31` are consumed by separate overlay/banner helpers (`sub_1013A7A()`, `sub_100E8E4()`, and a few later scene/UI paths).
  - `tile27/28` are consumed by popup/list helpers (`sub_10489E8()/sub_1048B14()` and `sub_101A13A()`), not by `scene_draw_status_panels()`.
- evidence: `scene_draw_status_panels()` assembly at `0x010146E4..0x01014710`; `sub_1045EA4()` hard-coded load list; `sub_1013A7A()` at `0x01013B04/0x01013B68`; `sub_10489E8()` at `0x01048A1C`; `sub_101A13A()` at `0x0101A152/0x0101A168`; targeted `py_eval` over `draw_map_tile_by_index()` xrefs filtered to tile ids `26/27/28/30/31/65`.
- next: treat the missing contract as status-panel-specific. The best next static target is no longer a broad shared UI-family initializer, but the one step that should have provided `tile26/UI2.gif` alongside the already scene-local `59..65` status-panel strip: either a dedicated background load omitted from `sub_1045EA4()` or a one-time bridge from the earlier named-image `UI2.gif` owner into scene tile `26`.

## 2026-06-03
- verified: the first in-scene draw ordering now makes the status/UI contract gap tighter. In `scene_runtime_tick()`, the normal draw pass is `scene_sort_actor_draw_order() -> scene_draw_softkey_overlay() -> scene_draw_status_panels() -> scene_draw_actor_pass() -> scene_build_interact_prompt_list()`. So the first `tile26` status-panel draw happens before later prompt/input helpers such as `scene_handle_hover_prompt_input()` can participate in that same pass.
- verified: among the currently identified `R9+0x5C64+0x15/+0x16/+0x17` writers, only `scene_runtime_init_and_sync()` is an unconditional pre-first-draw enabler on the current path. It sets `+0x15=1` and clears `+0x16/+0x17` at `0x01013780..0x01013786` before calling `sub_1045EA4()`. The other nearby writers are conditional and later-scoped: `scene_draw_softkey_overlay()` only raises `+0x17` during draw, `scene_handle_hover_prompt_input()` raises `+0x15` only on prompt/interaction input, and `sub_100ECDE()` is a prompt-state helper reused by both init and later hover/timer paths.
- verified: a mechanical scan for scene functions that both touch `R9+0x9AF0` and invoke the piclib vtable `+0xC` method found one real scene-side caller and one false positive. The real scene-side caller is `sub_10461D0()`, reached through `sub_1018660()`, and it invokes `(*piclib->vtable->loadTile)(piclib, 0x13)` when initializing a small widget object; this is tile `19`, not `tile26`. The other hit (`sub_1024448()`) only happened because a nearby non-piclib object also uses a `+0xC` callback in the same instruction window.
- verified: that new `+0xC` caller does not explain the crashing status-panel path. `sub_10461D0()` seeds a widget state that otherwise falls back to local `"loading.gif"` named-image handling, and its callers are `sub_1018660()` and the earlier startup-style `sub_103B0B0()`. There is still no confirmed scene-local explicit `loadTile(26)` caller before the first `scene_draw_status_panels()` pass.
- evidence: `江湖OL.CBE` IDA for `scene_runtime_tick()` (`0x01014D30`, draw-pass calls at `0x01015158..0x01015182`), `scene_runtime_init_and_sync()` (`0x01013780..0x010137B6`), `scene_draw_softkey_overlay()` (`0x01014972`), `scene_handle_hover_prompt_input()` (`0x01014B14`), `sub_10461D0()` (`0x010461D0`), and `sub_1018660()` (`0x01018660`); `py_eval` scan over main-CBE functions referencing `0x9AF0`.
- next: keep treating the active blocker as “status-panel drawing becomes eligible before any scene-native `loadTile(26)` request has run”. The next best static target is no longer generic `+0xC` scanning, but whichever scene-local UI/widget initializer should have requested `UI2.gif` alongside the already observed `UI3/UI4/UI7/UI8/UIsh7` pieces.

## 2026-06-03
- verified: the earlier non-scene `UI2/UI7/UI8` activity now splits cleanly into “shared lower-level loader” versus “scene-piclib slot state”. On the scene side, `sub_1043206()` is the only confirmed local file-backed tile loader, and it opens `\\JHOnlineData\\%s` through `sub_100D2BE()`. The earlier title/startup-style named-image paths also reach the same lower-level open helper (`sub_100D2BE()` shows up in the matching storage-trace opens for `UI8.gif` / `UI7.gif`), so there is a real shared resource/file-load layer below both systems.
- verified: there is still no confirmed upper-layer bridge from those earlier named-image owners into scene piclib slot state. The title-module transition screen `sub_A50()` stores its returned image handles in local fields at `R9+10348/10352/10356`, and the main CBE startup/update helper `sub_103B0B0()` stores `UI2/UI8/UI7` into its own startup object fields `+52/+54/+56`; neither path writes the scene piclib tables at `R9+0x9AF0`.
- verified: the scene-side “who should have requested tile26” answer is still negative but narrower. `scene_draw_status_panels()` only draws tile `26` / `65` and does not lazy-load; `scene_system_bootstrap()` only allocates the piclib and seeds the filename table; and the only confirmed scene-local explicit tile preloader before the crash remains `sub_1045EA4()`, which still hardcodes many ids but skips `25/26/29`.
- evidence: `江湖OL.CBE` IDA for `sub_1043206()` (`0x01043206`), `sub_100D2BE()` (`0x0100D2BE`), `scene_draw_status_panels()` (`0x0101466A`), and `sub_103B0B0()` (`0x0103B0B0`); `mmTitleMstarWqvga.cbm` IDA for `sub_A50()` (`0x0A50`); `bin/logs/storage_trace.log` newest session lines `609-620`.
- next: treat the remaining blocker as an upper-layer scene contract gap, not a raw file-open/cache gap. The next useful static target is the scene-local status/UI path that makes tile `26` drawable without going through `sub_1045EA4()` today, or that should have done so before `R9+0x5C64+0x15` is raised.

## 2026-06-03
- verified: the earlier `activeScreen=010534b4` path is now statically tied to the title/login dynamic module rather than the later scene/game module. In `mmTitleMstarWqvga.cbm`, `sub_C82()` builds a dedicated screen object via `sub_C54() -> sub_2CA()`, and that screen's init callback `sub_A50()` explicitly requests `UI2.gif`, `UI8.gif`, and `UI7.gif` by name.
- verified: this gives a narrower explanation for the earlier partial UI-family activity. The current storage trace shows `UI8.gif` and `UI7.gif` file reads before `mmGameMstarWqvga.cbm` is opened, matching the `sub_A50()` asset list and strongly suggesting the first `UI7/UI8` activity comes from a title-module post-login transition screen.
- verified: the same static pass also shows why the runtime gap is still specifically `tile26`. Even though the title-module transition screen knows about `UI2.gif`, its loads are module-local named-image requests, while the crashing scene path still depends on explicit scene-piclib slot fills (`sub_10433BA()` / `tileInfo[tileId].tileIndex`). So an earlier `UI2.gif` asset touch is not proof that scene tile `26` was seeded.
- evidence: `mmTitleMstarWqvga.cbm` IDA for `sub_C82()` (`0x0C82`), `sub_C54()` (`0x0C54`), `sub_A50()` (`0x0A50`), and strings `UI2.gif` / `UI8.gif` / `UI7.gif`; `bin/logs/storage_trace.log` newest session lines `609-620` and `627-635`.
- next: stop treating the earlier `UI7/UI8` seed as necessarily a scene-piclib-native path. The next narrow question is whether the missing scene contract is a bridge step from the title-module transition screen into the later scene piclib, or a completely separate explicit `loadTile(26)` path that simply never runs.

## 2026-06-03
- verified: the widened per-tile piclib trace now identifies the earlier caller that seeds `tile30/31`. In the newest clean run, `trace_piclib_slot_load` logs `tile31 -> UI8.gif` and `tile30 -> UI7.gif` at `tick=78`, both from guest LR `0x05019139/0x05019141` on the `activeScreen=010534b4` path, well before `scene_runtime_init_and_sync()` later starts the `sub_1045EA4()` preload at `tick=140`.
- verified: the scene preloader split is now concrete in runtime. During `scene_runtime_init_and_sync()` / `sub_1045EA4()` at `tick=140`, the only watched UI-family loads are `tile27 -> UI3.gif`, `tile28 -> UI4.gif`, and later `tile65 -> UIsh7.gif`; there are still no `trace_piclib_slot_load` entries at all for `tile25`, `tile26`, or `tile29`.
- verified: that means the current live gap is even narrower than the previous `25/26/29/30/31` bucket. At the first crashing status-panel draw, the actually missing set is `tile25`, `tile26`, and `tile29`, while `tile27/28/30/31/65` are all already resident.
- evidence: `bin/logs/net_trace.log` newest session lines `2747-2756`, `3483-3527`, and `3661-3666`; `bin/logs/stdout_trace.log` newest session tail.
- next: static RE should pivot from “who seeds `30/31`” to “what is the earlier screen/module path behind LR `0x05019139/0x05019141`, and why does it stop after `UI7/UI8` instead of also requesting `UI1/UI2/UI6` (`25/26/29`) before scene status draw becomes visible”.

## 2026-06-03
- verified: widened status-slot tracing shows the missing scene UI preload is narrower than the previous whole-family guess. By the last pre-crash snapshot, `tile30` and `tile31` are already loaded (`2/05025c28`, `1/05025c18`) from an earlier path at `tick=70`, `tile27/28` load later during `sub_1045EA4()` at `tick=312`, and `tile65` then loads as `UIsh7.gif`; only `tile25`, `tile26`, and `tile29` stay at `tileIndex=-1` through the first `scene_draw_status_panels()` crash.
- verified: the first crash is still the exact same local status-panel background draw. After the fresh-enter callback cluster, the scene reaches `trace_scene_runtime_tick label=draw_pass` and then `label=status_panels` at `tick=314`; the very next draw is still `draw_map_tile_by_index(tileId=26, ...)` with `tileIndex=-1` and `resPtr=0`, followed by the same null-image abort in `draw_image_tiled_clipped()`.
- verified: the scene-list wrapper fix is now confirmed on the live path. `scene_parse_column_names()` now logs sane short labels for both `0x1E/3` (`id/room/scene/state`) and `0x1E/7` (`name/job/level/state`), and both `scene_roomlist_return` and `scene_roles_return` are reached before the later status-panel crash.
- evidence: `bin/logs/net_trace.log` newest session lines around `9828-9831`, `11764`, `12445-12448`, `12485-12489`, and `12977-12994`; `bin/logs/stdout_trace.log` newest session tail.
- next: stop treating this as a full `UI1..UI8` family miss. The next narrow target should be the earlier loader path that already seeds `tile30/31` but never requests `tile25/26/29`, ideally by widening `trace_piclib_slot_load` from `{26,65}` to `{25,26,29,30,31,65}` and then reproducing once.

## 2026-06-03
- changed: widened the read-only status-panel tile watcher in `src/main.c` so `trace_status_tile_slots` now also logs status bytes `R9+0x5C64+0x15/+0x16/+0x17` and the scene piclib slots for `tile25..31` plus `tile65`, instead of only `tile26/65`.
- verified: the current missing preload is broader than a single `UI2.gif` slot in static code. The fixed piclib table maps `tile25..31` to `UI1.gif`, `UI2.gif`, `UI3.gif`, `UI4.gif`, `UI6.gif`, `UI7.gif`, and `UI8.gif`, while the confirmed scene preloader `sub_1045EA4()` only requests `27/28` from that family and still skips `25/26/29/30/31`.
- verified: the status-panel flag bytes now have a more concrete role split in static code. `scene_runtime_init_and_sync()` sets `R9+0x5C64+0x15 = 1` and clears `+0x16/+0x17` at `0x01013780..0x01013786`, `scene_draw_status_panels()` later clears only `+0x16/+0x17` at `0x010147C0/0x010147C2`, `scene_draw_softkey_overlay()` raises `+0x17`, and actor/parser paths such as `sub_100F094()` raise `+0x16`.
- evidence: IDA disassembly for `sub_10434CE()`, `sub_1045EA4()`, `scene_runtime_init_and_sync()`, `scene_draw_status_panels()`, `scene_draw_softkey_overlay()`, `sub_100ECDE()`, `scene_handle_hover_prompt_input()`, `net_handle_group_info()`, and `sub_10159DA()`. Nearby static code around those flag writers shows visibility/dirty writes but no matching scene piclib `loadTile(25/26/29/30/31)` call.
- next: rerun one clean login-to-scene reproduction and inspect the first widened `trace_status_tile_slots ... flags=... 25=... 26=... 30=... 31=... 65=...` lines around `0x01013780` and the later `scene_draw_status_panels()` crash. The key question is whether the first bad draw is just `tile26`, or whether the entire `UI1/UI2/UI7/UI8` status family remains unseeded when `+0x15` is first raised.

## 2026-06-03
- verified: the two recurring text families in recent logs are now separated. `guestCaller=01035205` is still the scene-overlay `正在加载中...` path via `sub_100337C(0)`, but `guestCaller=01034bc7/01034c15` is a different multiline text renderer printing `正在和服务器通讯请稍候....`.
- evidence: newest `bin/logs/net_trace.log` shows `lcd_text_call ... guestCaller=01034bc7` / `01034c15` drawing the characters `正 在 和 服 务 器 通 讯 请 稍 候 ....`; IDA shows `0x01034BC7` and `0x01034C15` are inside `sub_1034AC8`, a width-aware multiline text drawing helper, not the `sub_100337C(0)` scene-overlay helper.
- verified: after the first fresh-enter `event=7` cluster, there are no new `trace_loading_overlay_call` entries in the same session tail.
- evidence: newest run reaches `runtime_state label=post_data_event dispatch=01012e4d ... sceneGate=0 sceneState=7 activeScreen=01053450` at `tick=138` after `kind=30 subtype=1/3/7` and `trace_followup_scan_enter`; later lines in the same tail show no new `trace_loading_overlay_call` hits.
- next: treat `Post-Login Loading Crash` as largely moved past in the current branch. The next useful trace target should be the first normal in-scene tick / UI mode / actor-content state after `tick=138`, not another retry on loading text families.

## 2026-06-03
- changed: added a read-only `trace_scene_tick_gate_change` watcher for `R9+23655/+23656` and reran once to separate the actual `scene_runtime_tick()` loading gate from the older `loadFlags=...` bytes.
- verified: the two-byte scene-tick gate is already raised during fresh-enter init, so the current run is no longer blocked by `scene_runtime_tick()` staying in its early `sub_1003568()` branch.
- evidence: newest `bin/logs/net_trace.log` shows `trace_scene_tick_gate_change pc=010132ca ... prev=0,0 now=1,0` immediately followed by `pc=010132cc ... now=1,1` at `tick=231`, both inside `scene_runtime_init_and_sync()`. The same session then continues through `kind=30 subtype=1`, `kind=30 subtype=3`, `kind=30 subtype=7`, and `trace_followup_scan_enter` at `tick=232` without any later `trace_scene_tick_gate_change` back to zero. All later `trace_loading_overlay_call` lines still come from scene-init/rebuild callsites and already report `sceneTickGate=1,1`.
- verified: `loadFlags=1,0,0` should no longer be treated as synonymous with the local loading-overlay gate in this phase.
- evidence: `runtime_state label=post_data_event ... loadFlags=1,0,0` persists after `tick=232`, but the separate scene-tick gate bytes are already `1,1`, and the static disassembly of `scene_runtime_tick()` uses the latter (`R9+0x5C64+3/+4`) for its early loading draw branch, not the former.
- next: treat the original post-login loading blocker as largely moved past. The next focused trace target should be the first in-scene mode/content/state transition after fresh-enter callback completion, not another retry on `0x1E/3/7` shell coverage or the old loading gate bytes.

## 2026-06-03
- changed: added a read-only `trace_scene_tick_gate_change` watcher in `src/main.c` for `R9+23655/+23656`, and extended `trace_loading_overlay_call` to print those same two bytes alongside the older `overlayState` byte at `R9+19638`.
- verified: the current persistent scene loading overlay is gated by `scene_runtime_tick()` bytes `R9+0x5C64+3/+4`, not by the older `runtime_state loadFlags=%u,%u,%u` bytes at `R9+23673..23675`.
- evidence: IDA disassembly of `scene_runtime_tick()` shows `0x01014D74 .. 0x01014D80` checking `LDRB [R4,#3]` and `LDRB [R4,#4]` where `R4=R9+0x5C64`; if either is zero, the function calls `sub_1003568()` at `0x01014D80`, then `sub_1013BDC()`, and returns down the early loading path. The newer xref pass also shows `scene_runtime_tick()` has its own direct call to `sub_1003568()`, which explains later `正在加载中...` draws without requiring `scene_runtime_init_and_sync()` itself to loop forever.
- next: rerun once with the new watcher and capture the first `trace_scene_tick_gate_change ... prev=..., ... now=..., ...` line plus nearby `trace_loading_overlay_call` lines. That should show which code path never raises the second ready byte and whether the missing contract is local scene-asset completion or another scene/business packet side effect.

## 2026-06-03
- changed: added read-only `trace_loading_overlay_call` hooks in `src/main.c` at `sub_1003568`, `sub_100337C`, and `sub_1003CFC` to record who re-enters the local loading-overlay helper and what overlay state byte (`R9+19638`) it sees.
- verified: the repeated `"正在加载中..."` overlay is now traced back to a local scene-init helper chain, not just to a generic startup/update screen guess.
- evidence: newest `bin/logs/net_trace.log` shows repeated `lcd_text_call ... guestCaller=01035205 ... text=正在加载中...`; saved `R0=01003648`, and static xrefs show `0x01003648` is the GBK string `正在加载中...` referenced only by `sub_100337C`. IDA confirms `sub_100337C(0)` calls `sub_10351BA(&unk_1003648, 120, 160, 0xFFFFFF)`, and `scene_runtime_init_and_sync()` calls `sub_1003568()` immediately after `sub_1010228()`, while `sub_1003568()` is just `return sub_100337C(0);`.
- next: rerun once with the new overlay-call trace and inspect whether later post-enter frames keep coming from `sub_1003568`, `sub_1003CFC`, or another caller. That should tell us whether the local loading overlay is stuck because one init path never clears, or because a timer/state loop keeps re-arming it.

## 2026-06-03
- changed: extended `lcd_text_call` in `src/main.c` again so it now reads the `vMDrawString` wrapper's saved register block at `SP+0xCC` and logs `guestCaller` from the saved LR slot (`SP+0xEC`), plus the full saved `{R0-R7,LR}` values.
- verified: the first `lcd_text_call` pass was useful but still only reached the generic guest LCD manager wrapper, not the higher-level loading-screen function.
- evidence: newest `bin/logs/net_trace.log` shows repeated `"正在加载中..."` draws with stable `lr=0100488d` / `last=0100488a`, which are inside `sub_1004836`, the generic `vMDrawString` wrapper. The varying `st[0]` values (`fefefefe`, `050195e4`, `01055970`, `0`, `8`, `6`) indicate caller context is preserved in the wrapper frame rather than in current LR.
- next: rerun once with the new saved-LR trace and inspect `guestCaller=` for the repeated `"正在加载中..."` lines. That should reveal the actual guest function or screen draw path maintaining the overlay.

## 2026-06-03
- changed: extended the LCD text tracing in `src/main.c` so plain `vMDrawString` and `vMDrawStringEx` now also emit the richer `lcd_text_call` trace with guest register/stack context, matching the existing clip/rect string APIs.
- verified: the newest clean rerun confirms that the first `0,0,0 -> 1,0,0` transition is triggered by `scene_runtime_init_and_sync()`, not by `net_handle_group_info()`.
- evidence: `bin/logs/net_trace.log` newest session shows `trace_sub_1010228_callsite label=scene_runtime_init_and_sync pc=01013594 ... flagsBefore=0,0` immediately followed by `trace_sub_1010228_entry pc=01010228`, `trace_sub_1010228_entry pc=0101022a`, and finally `trace_loading_flags_change pc=0101029c ... now=1,0,0`. No `label=net_handle_group_info` line appears in that session.
- next: rerun once with the new `lcd_text_call api=vMDrawString` trace and identify the guest caller that repeatedly paints `"正在加载中..."`. That should tell us whether the overlay is still owned by a loading screen/screen object even after scene-init has already advanced.

## 2026-06-03
- changed: added read-only `trace_sub_1010228_callsite` hooks in `src/main.c` at the only two static callers of `sub_1010228()` (`scene_runtime_init_and_sync()` at `0x01013594` and `net_handle_group_info()` at `0x010124AA`), and widened the existing entry hook to also try `0x0101022A` in case the first Thumb instruction boundary was being missed.
- verified: the latest rerun still shows the first `0,0,0 -> 1,0,0` transition at `pc=0101029c`, but no `trace_sub_1010228_entry` line was emitted anywhere in that same session, so the missing entry trace cannot be used as evidence that the function did not execute.
- evidence: newest `bin/logs/net_trace.log` session contains `trace_loading_flags_change pc=0101029c ... now=1,0,0`, later scene-enter `trace_business_handler pc=01039b8a kind=30 subtype=1`, and a full-text search returns no `trace_sub_1010228_entry`; IDA still shows only two code xrefs to `sub_1010228()` (`0x01013594`, `0x010124AA`).
- next: rerun once with the new callsite hooks and inspect which `trace_sub_1010228_callsite label=...` line appears in the same session as the first transition.

## 2026-06-02
- changed: split the current mock's overloaded `motionResource` guess away from the scene-text path. `src/main.c` now routes both the synthetic `0x1B/12.name` field and the second trailing `actorinfo` string through a dedicated `vm_net_mock_scene_key_name()` helper, with optional override `CBE_SCENE_KEY` and default value matching the existing GBK `c00蓬莱仙岛_01` scene key.
- verified: this is a minimal protocol experiment only; it does not yet prove whether the canonical wire value should be the extensionless key or the full `.sce` filename.
- evidence: `parse_actorinfo_response()` already copies that trailing `actorinfo` string into `R9+0x5E46`, and `ui_apply_named_posinfo_target()` already copies `0x1B/12.name` into the persistent `+1141` scene-text slot. Reusing one dedicated scene-key helper is therefore closer to observed consumer semantics than continuing to feed both from `motionResource`. `make` rebuilt successfully after the change.
- next: rerun one clean login-to-scene session and inspect whether the wrong actor/motion-resource path disappears, and whether later resource opens now pivot toward scene-key-compatible assets or requests.

## 2026-06-02
- verified: `R9+0x5E46` is first written from login `actorinfo`, not first synthesized inside fresh scene-enter.
- evidence: disassembly of `parse_actorinfo_response()` (`0x0100FA88`) shows a late trailing string field copied into `R9+0x5E46` at `0x0100FD00 .. 0x0100FD08` via `sub_100FA56(..., R9+0x5E46, 0x1E)`, and the same function returns `R9+0x5E46` at `0x0100FD24 .. 0x0100FD2E`. `scene_runtime_init_and_sync()` later only validates/reuses that buffer before passing it to `scene_rebuild_runtime_nodes()`.
- verified: the current mock likely mislabels that `actorinfo` slot. `src/main.c:vm_net_mock_build_actor_info()` uses the final `string, scalar, string, scalar, scalar` sequence for `actorResource`, `actorResourceArg`, `motionResource`, `arg0`, `arg1`, but the consumer side now points much more strongly at “scene/map key or scene resource id” for the second trailing string than at a motion resource filename.
- evidence: local assets already include `bin/JHOnlineData/c00蓬莱仙岛_01.sce` and `c00蓬莱仙岛_03.sce`, while the validator for `R9+0x5E46` / `uiState+1141` explicitly accepts `'c'`-prefixed strings and the current outgoing mock `mapID` is the romanized placeholder `c00PenglaiXiandao_01`.
- next: stop coupling that `actorinfo` trailing string to `motionResource` in future experiments. The next narrow test should use a dedicated scene-key helper and determine whether the client wants a full `.sce` filename or an internal extensionless scene key.

## 2026-06-02
- verified: `uiState+1141` has a third explicit writer besides `27/12` and `scene_rebuild_runtime_nodes()`: `net_handle_actor_move_info()` case `12` clears the slot and copies incoming field `name` there.
- verified: `sub_100EEE0()` is a fallback-label collector, not another direct writer of `uiState+1141`.
- evidence: disassembly of `net_handle_actor_move_info()` (`0x01012DDE .. 0x01012E10`) shows `name` is read from the packet and copied into `uiState+1141`. Disassembly/decompilation of `sub_100EEE0()` shows it first validates `uiState+1141` with `sub_100EEBC(uiState+1141)` and only when invalid copies its `displayNamePtr` into a separate 4-entry table at `R9+0x5CE4`; current callers are `sub_100F094()`, `sub_10159DA()`, and `scene_parse_npcinfo_and_spawn_npcs()`.
- next: the main unresolved question is no longer “is `+1141` a display-name slot at all”, but “which upstream packet/path first gives it the c-prefixed scene key that fresh-enter later reuses as `mapID`”.

## 2026-06-02
- verified: the fresh-enter temporary text slot `R9+0x5E46` and the persistent `uiState+1141` slot share the same validator, and that validator explicitly accepts `'c'`-prefixed strings.
- evidence: `sub_100EEBC()` (`0x0100EEBC`) returns success if either the compare callback at `R9+0x4DA0 -> +0x48` accepts the string or `*input == 'c'`. `scene_runtime_init_and_sync()` uses this helper on `R9+0x5E46` in the fresh-enter path and uses `sub_100FA40(uiState+1141)` in the restore path. Runtime logs already show outgoing `mapID=c00PenglaiXiandao_01`, which fits this validator well.
- next: treat the slot as likely holding a c-prefixed internal scene/map key. The next best RE target is the producer that first formats `R9+0x5E46`, not the later resource misroute that consumes it.

## 2026-06-02
- verified: the shared `uiState + 1141` buffer has two explicit writers, and fresh-enter versus restore path treat them differently.
- evidence: IDA decompilation/disassembly shows `ui_apply_named_posinfo_target()` (`0x0100E9B8`) clears and copies incoming `0x1B/12.name` into `uiState+1141`, while `scene_rebuild_runtime_nodes()` (`0x0100F7A6`) clears and copies its `currentMapIdText` argument into the same buffer only when `varg_r0_1 != 0`. In `scene_runtime_init_and_sync()`, the fresh scene-enter path (`0x010132A8 -> 0x01013586`) prepares `currentMapIdText=R9+0x5E46` and calls `scene_rebuild_runtime_nodes(varg_r0_1=1, ...)`, but the pending scene-switch/restore path (`0x010131C4 -> 0x010131FE`) instead validates existing `uiState+1141` with `sub_100FA40(uiState+1141)` and calls `scene_rebuild_runtime_nodes(varg_r0_1=0, ...)`, which preserves the old buffer contents.
- verified: `scene_send_map_enter_request()` is a direct consumer of the same slot, reading `uiState+1141` as outgoing WT field `mapID`.
- next: treat `uiState+1141` as a persistent current-scene/current-location text slot, not an actor-resource slot. The next narrow RE target is the producer of the fresh-enter temporary text buffer at `R9+0x5E46`.

## 2026-06-02
- verified: the current synthetic `0x1B/12` is not answering a generic scene packet or a resource request; it is answering the embedded `0x1B/11` subrequest inside the post-enter `len=86` composite packet.
- evidence: newest `bin/logs/net_trace.log` shows `trace_scene_send_map_enter_request`, then a `len=86` WT packet with object sequence `1/0x0C/1`, `1/7/42`, `1/2/3(maptype,mapID,exitID)`, `1/0x1B/11`, `1/0x0C/1`, `1/7/42`, followed by `mock_scene_change_combo_response ... fb11=1 fb12=1`. Static code in `src/main.c` only appends `0x1B/12` when the request contains `1/0x1B/11`.
- verified: `0x1B/12.name` aliases the same UI buffer later used as outgoing `mapID`.
- evidence: IDA decompilation shows `scene_send_map_enter_request()` (`0x0100F9B4`) writes request field `mapID` from `*(_DWORD *)(R9+21676) + 1141`, while `ui_apply_named_posinfo_target()` (`0x0100E9B8`) writes incoming server field `name` from `0x1B/12` into that same `+1141` buffer. This sharply weakens the earlier actor-resource guess and supports a map/scene/location-text interpretation instead.
- next: stop guessing resource filenames for `0x1B/12.name`; the next useful static target is the producer/consumer chain for that shared `+1141` map/name buffer around scene-enter and `27/11 -> 27/12`.

## 2026-06-02
- changed: replaced the synthetic `0x1B/12.name` fallback in `src/main.c` from the bundle filename `MMORPGTempcbm` to the selected motion-resource name helper: honor `CBE_ACTOR_MOTION_RESOURCE` when explicitly set, otherwise fall back to the existing real actor resource chosen by job/sex instead of the hand-authored `mock_missing_motion.actor`.
- verified: the latest direct callsite trace closes the upstream caller chain. `scene_rebuild_runtime_nodes()` at `0x0100F8F6` is the first observed caller that passes `stack0Name=MMORPGTempcbm`, which then becomes `arg4Name=MMORPGTempcbm` at `parse_actor_motion_descriptor()` entry and finally a direct `load_resource_stream_by_name()` open.
- evidence: newest `bin/logs/net_trace.log` shows `trace_sub_10352ae_call_from_scene_rebuild ... stack0Name=MMORPGTempcbm uiName=MMORPGTempcbm`, followed by `trace_parse_actor_motion_entry ... arg4Name=MMORPGTempcbm uiName=MMORPGTempcbm` and `trace_resource_stream_call_from_actor_motion ... name=MMORPGTempcbm uiName=MMORPGTempcbm`. No competing `trace_sub_10352ae_call_from_1036768` line carries that same string in the observed path. `make` rebuilt successfully after switching the synthetic fallback name.
- verified: the follow-up rerun confirms that plain real `.actor` files are still the wrong format for this slot. The path now forwards `h_warrior.actor` into `parse_actor_motion_descriptor()`, opens it successfully, and then immediately tries to open a malformed concatenated pseudo-name (`iorwalk1.gif...h_warriorwalk2.gif...jian.gif`), which is consistent with schema mismatch rather than a missing file.
- evidence: newest `bin/logs/net_trace.log` shows `trace_sub_10352ae_call_from_scene_rebuild ... stack0Name=h_warrior.actor`, then `trace_parse_actor_motion_entry ... arg4Name=h_warrior.actor`, then direct open of `h_warrior.actor`. `bin/logs/storage_trace.log` shows that file opens and reads normally, followed by `file_open_fail path=JHOnlineData/iorwalk1.gif...h_warriorwalk2.gif...jian.gif`. A scan of local `bin/JHOnlineData/*.actor` files also shows all tracked samples currently use type byte `0x02` at offset `+4`; none match the mock motion-wrapper type `0xF1`.
- next: stop guessing among existing local `.actor` files for this field. The next real blocker is the absence of a confirmed genuine motion-descriptor sample or naming rule for the resource consumed by `parse_actor_motion_descriptor()`.

## 2026-06-02
- changed: added direct callsite traces for the two known `sub_10352AE()` callers that matter here: `scene_rebuild_runtime_nodes()` at `0x0100F8F6` and `sub_1036768()` at `0x010368C4`.
- verified: the new `parse_actor_motion_descriptor()` entry trace shows the misrouted string is already present in the first stacked extra argument before any parser-local processing, while `R0` remains a non-string object/parser pointer. The direct caller is consistently the indirect `BLX` inside `sub_10352AE()` (`LR=010352D7`).
- evidence: newest `bin/logs/net_trace.log` session shows `trace_parse_actor_motion_entry ... arg4Name=mock_missing_motion.actor` first, later `... arg4Name=MMORPGTempcbm ... uiName=MMORPGTempcbm`, and the paired `trace_resource_stream_call_from_actor_motion` lines consume the same pointer. Static decompilation of `scene_rebuild_runtime_nodes()` shows it copies `currentMapIdText` into UI buffer `+1141` and then immediately calls `sub_10352AE()`, which matches the observed lockstep but is not yet directly confirmed as the active caller.
- next: rerun and inspect `trace_sub_10352ae_call_from_scene_rebuild` versus `trace_sub_10352ae_call_from_1036768` to confirm which upstream caller first supplies `MMORPGTempcbm` as the stacked extra argument.

## 2026-06-02
- changed: added one more read-only tracepoint at `parse_actor_motion_descriptor()` entry (`0x0100D6E2`) to log the caller `LR` plus the incoming `arg0/arg4/arg8/argC` pointers before the later `0x0100D6FC` call forwards `arg0` into `sub_100D534()`.
- verified: by the end of the previous run, the direct offending callee was already confirmed (`parse_actor_motion_descriptor()`), but the upstream caller that first supplied `MMORPGTempcbm` was still unknown.
- evidence: existing trace already showed `trace_resource_stream_call_from_actor_motion ... name=MMORPGTempcbm uiName=MMORPGTempcbm`; the new trace is intended to separate whether that value is already present in `arg0` at function entry and to record the caller `LR` for the next rerun. `make` rebuilt successfully after adding the entry trace.
- next: rerun and inspect `trace_parse_actor_motion_entry` together with `trace_resource_stream_call_from_actor_motion` to identify the upstream caller and source argument for the misrouted `MMORPGTempcbm` string.

## 2026-06-02
- changed: used the narrower `load_resource_stream_by_name()` tracepoints to identify the first direct caller that reuses the `0x1B/12`-derived UI string as a resource name.
- verified: the active offending caller is `parse_actor_motion_descriptor()` at callsite `0x0100D6FC`, not the pending-download path. On the newest run, after `kind=27 subtype=12` sets the UI cached name to `MMORPGTempcbm`, the trace sequence is `trace_resource_stream_call_from_actor_motion -> trace_resource_stream_wrapper_entry -> trace_load_resource_stream_entry -> trace_resource_open_helper`, all with `name=MMORPGTempcbm uiName=MMORPGTempcbm`.
- evidence: newest `bin/logs/net_trace.log` session shows `trace_business_handler pc=01010bc0 ... kind=27 subtype=12` at `tick=122`, then at `tick=124` the exact chain above carrying `MMORPGTempcbm`; no `trace_update_request_prepare` appears for that string. In contrast, sibling callsites `0x0100D570` and `0x0100DB9C` continue to load ordinary actor/UI resource names (`h_warrior.actor`, `ui_h_war.actor`, `b_bamboo.actor`, etc.).
- next: inspect the live upstream source for the name pointer consumed at `parse_actor_motion_descriptor()` callsite `0x0100D6FC` and determine which scene-node/member or cached label field is being mistaken for a motion-resource name.

## 2026-06-02
- changed: added a second, narrower round of read-only tracepoints around `load_resource_stream_by_name()`: entries at `0x0100D48A` / `0x0100D534`, plus the three known upstream callsites into `sub_100D534()` at `0x0100D570`, `0x0100D6FC`, and `0x0100DB9C`.
- verified: the newest runtime trace rules out the earlier “`MMORPGTempcbm` first reappears through pending download/update” branch. After `0x1B/12` is consumed, the first later reuse of the same string is a direct `load_resource_stream_by_name() -> sub_100D2BE()` open, not a `sub_1044DA0()/sub_1044EF6()` pending-entry path or a `type=6` serialization.
- evidence: latest `bin/logs/net_trace.log` shows `trace_business_handler pc=01010bc0 ... kind=27 subtype=12` at `tick=133`, then later `trace_resource_open_helper ... lr=0100d49d ... name=MMORPGTempcbm uiName=MMORPGTempcbm` at `tick=135`; no corresponding `trace_resource_pending_lookup/mark` or `trace_update_request_prepare` line appears for `MMORPGTempcbm`. `make` rebuilt successfully after adding the narrower caller traces.
- next: rerun one clean session and capture the new `trace_load_resource_stream_entry`, `trace_resource_stream_wrapper_entry`, `trace_resource_stream_call_from_d564`, `trace_resource_stream_call_from_actor_motion`, and `trace_resource_stream_call_from_db82` lines to identify which upstream caller first treats the `0x1B/12` display string as a resource name.

## 2026-06-02
- changed: added read-only resource-name flow tracepoints in `src/main.c` for `sub_100D2BE()` (`0x0100D2BE`), `sub_1043206()` (`0x01043206`), `sub_1044DA0()` (`0x01044DA0`), `sub_1044EF6()` (`0x01044EF6`), and `send_update_chunk_request()` (`0x01036C66` case `n2==4`). Each trace records the resource/download `name`, caller `LR`, and the current active UI cached name from the `0x1B/12` path.
- verified: static IDA analysis now narrows the resource-open bridge to a very small set. `sub_100D2BE()` has only four code xrefs in `江湖OL.CBE`; the two relevant post-login/resource-update candidates are `sub_1043206()` and `sub_1044DA0()`. Separately, `send_update_chunk_request()` case `4` serializes its later `type=6.name` from the current pending entry at `*(R9+38284)+2`.
- evidence: IDA xrefs for `0x0100D2BE` point only to `sub_100D338`, `load_resource_stream_by_name`, `sub_1043206`, and `sub_1044DA0`; decompilation of `send_update_chunk_request()` shows case `n2==4` reading `name` from `*(R9+38284)+2`, `type` from the first `u16` of that entry, `start` from offset `+52`, and `version` from `R9+38328`. `make` rebuilt successfully after adding the tracepoints.
- next: rerun one clean session and use the new `trace_resource_open_helper`, `trace_piclib_resource_dispatch`, `trace_resource_pending_lookup`, `trace_resource_pending_mark`, and `trace_update_request_prepare` lines to identify the first caller that turns the `0x1B/12` display/target name into a resource-download key.

## 2026-06-02
- changed: narrowed the semantic split between the post-scene `0x1B/12` business packet and the later `type=6` resource request using IDA, without changing emulator behavior.
- verified: `0x1B/12.name` is not a resource filename field. `net_handle_fb_target_dispatch()` subtype `12` only forwards it into `ui_apply_named_posinfo_target_with_fb() -> ui_apply_named_posinfo_target()`, which caches `name` in active UI state and applies it as a named target/location.
- verified: the later `type=6` request `name` is a resource filename / pending-download key. `send_update_chunk_request()` serializes `name/screen/type/start/version`, and that pending `name` comes from the local resource-miss path `sub_1044DA0()/sub_1044EF6()` after `sub_100D2BE()` fails to open a file by name.
- evidence: IDA `0x01010BC0 -> 0x0100EC66 -> 0x0100E9B8` for the `0x1B/12` chain; `0x01044DA0 -> 0x01044EF6 -> 0x01036C66` for the resource-miss/download chain. `ui_apply_named_posinfo_target()` reads fields `name` and `posinfo`, while `send_update_chunk_request()` emits fields `name`, `screen`, `type`, `start`, `version`.
- next: identify the intermediate caller that wrongly routes the named-target/display string into a local resource open path. That is now the most likely explanation for why a synthetic `0x1B/12.name` can still steer later `type=6` traffic.

## 2026-06-02
- changed: added per-process `session_start` markers to both `logs/net_trace.log` and `logs/storage_trace.log`, written on first log use so appended reruns can be separated without deleting prior evidence.
- verified: the original post-login loading/update loop is now past its main blocker. In the newest appended run, the `Codex` reopen failure disappears, `JHOnlineData/MMORPGTempcbm` opens successfully from the normal local loader path, and the update callback tears down the loading screen with `loadFlags=0,0,0`.
- evidence: `bin/logs/storage_trace.log` newest tail shows `file_open handle=5 path=JHOnlineData/MMORPGTempcbm mode=rb` and `file_read ... hex=fefefefe`, with no later `file_open_fail path=JHOnlineData/Codex`. `bin/logs/net_trace.log` lines `105181-105188` show event `7` dispatch into `0x0103489B`, `screen_manager idx=6 remove r0=0105a518`, `runtime_state ... loadFlags=0,0,0 activeScreen=01056204`, and `after_data_event updateState=2 downloadState=0 hasLocalUpdate=1 flags=1,1,1,1`.
- next: isolate the next clean run with the new `session_start` markers and follow the first post-teardown screen/resource transition instead of continuing to treat the old `Codex` update loop as the active blocker.

## 2026-06-02
- changed: narrowed the next post-install semantic by changing only the synthetic `0x1B/12` `name` field in `src/main.c` from `"Codex"` to `"MMORPGTempcbm"`.
- verified: the host binary-mode fix fully resolved the previous runtime-contract blocker. `MMORPGTempbin` now writes exact binary sizes, closes cleanly, and renames to `MMORPGTempcbm`; the remaining loop happens later and is tied to a reused logical name.
- evidence: latest `bin/logs/storage_trace.log` shows `file_open ... MMORPGTempbin mode=w+b`, exact `file_getfilesize` progression `0 -> 20480 -> 40960`, final `file_close`, and `file_rename ... finalNew=JHOnlineData/MMORPGTempcbm success=1`. Immediately afterward the same log shows repeated `file_open_fail path=JHOnlineData/Codex mode=rb`, while `bin/logs/net_trace.log` shows the repeated `type=6` update requests themselves still carry `name="Codex"`. IDA `sub_100D2BE` is the generic `\\JHOnlineData\\%s` loader helper at the failing open site.
- next: rerun and confirm whether replacing that synthetic `0x1B/12 name` with the installed resource name removes the `Codex` open failures and stops the repeated update loop.

## 2026-06-02
- changed: generalized the host-side binary-open fix instead of keeping it scoped to update temp files. `vm_file_select_mode()` in `src/vmFunc.c` now appends `b` automatically when guest hints omit it, and direct host-side append/write helpers were switched from text modes (`"a"`, `"w"`) to binary-safe modes (`"ab"`, `"wb"`).
- verified: this turns the earlier `MMORPGTempbin` finding into a broader emulator rule: guest payloads and trace/output files should never go through host text mode on Windows, because newline translation can corrupt byte-exact protocols and downloaded assets.
- evidence: the active `MMORPGTempbin` blocker came from a host `w+` open path that inflated file sizes during binary writes; the new change removes the same class of risk from the central guest file-open helper and the remaining direct host `fopen` write/append sites.
- next: rerun and confirm the update temp file now stays byte-exact through completion, with no further text-mode drift in future guest-created files.

## 2026-06-02
- changed: tightened host file-open semantics for update cache files in `src/vmFunc.c`; `vm_file_ext_requires_binary()` now treats no-extension `*tempbin` paths and `.bin` files as binary-open targets so the client's `w+` hint becomes `w+b` on Windows instead of text mode.
- verified: the current post-`27/4` blocker is no longer just “some later packet may be missing”. Static code plus storage traces now show the update/install path is reached, but it never satisfies its own completion condition because host file sizes are inflated during writes.
- evidence: latest `bin/logs/net_trace.log` shows `mock_type27_followup_combo_response`, `trace_business_handler pc=01010bc0 ... kind=27 subtype=4`, then repeated `type=6` update requests and `mock_update_chunk_response name=MMORPGTempcbm start=...` until the final chunk at `start=45056 size=3802`. In parallel, `bin/logs/storage_trace.log` shows `MMORPGTempbin` opened as `w+`, three writes of `20480`, `20480`, and `7898`, but intervening host sizes `20567` and `41153`; IDA `sub_1037000` (`0x01037000`) only closes and installs the file when `current_filesize + chunk_len == totalsize`, so the text-mode size inflation blocks `sub_10368CA()` / `sub_1036768()` completion.
- next: rerun with the rebuilt binary and confirm that `MMORPGTempbin` now grows as exact binary byte counts, then check for the expected `file_close` / `file_rename` follow-up and whether the update/loading screen exits automatically.

## 2026-06-02
- changed: added a dedicated built-in response for the newly exposed `len=34` type-27 follow-up composite request. Requests carrying `0x1B/4` now receive a combo reply with `0x19/12 result=4` (when `0x19/5` is present), `0x1B/11`, `0x1B/4(min=0,result=1,type=request.type,fb=1,info=\"\")`, and `0x07/42(booknum=0,booksinfo=\"\")`.
- verified: the prior `0x1B/12` experiment was the right direction. It did not clear loading by itself, but it immediately unlocked a new client request after scene-change: `len=34` containing `0x19/5`, `0x1B/11`, `0x1B/4(type=1)`, and `0x07/42`.
- evidence: latest `bin/logs/net_trace.log` shows `mock_scene_change_combo_response objects=5 ... fb12=1 len=214`, then `trace_business_handler pc=01010bc0 ... kind=27 subtype=12`, followed by `trace_scene_send_type27_followup_request type=1` and a new `send_payload len=34 ... 011905 ... 011b0b ... 011b04 ... type=1 ... 01072a`; static code for the later `27/4` follow-up path reads fields `min`, `result`, `type`, `fb`, and `info`.
- next: rerun with the rebuilt binary and check whether `mock_type27_followup_combo_response` appears, whether `trace_followup_case label=case27_4` finally fires, and whether the loading state moves again afterward.

## 2026-06-02
- changed: extended the composite scene-change response experiment again so that requests carrying `0x1B/11` now receive both the existing empty `0x1B/11` object and a minimal `0x1B/12` object with `result=1`, `fb=1`, `name="Codex"`, and `posinfo=(120,120)`.
- verified: the previous trace proved `0x1B/11` alone is not enough. The client does enter the primary `net_handle_fb_target_dispatch()` for subtype `11`, but static code shows that handler only implements meaningful logic for subtype `3`, `6`, and `12`; meanwhile the later follow-up case `27/11` branches on the stored `fb` byte that only subtype `12` writes.
- evidence: latest `bin/logs/net_trace.log` shows `trace_business_handler pc=01010bc0 ... kind=27 subtype=11` plus the follow-up `case27_11`, but still no `trace_scene_send_type27_followup_request`; IDA decompilation of `net_handle_fb_target_dispatch()` shows subtype `12` reads `result` and calls `ui_apply_named_posinfo_target_with_fb()`, which stores field `fb` at the same `+0x3D0` byte later tested by both `scene_runtime_init_and_sync()` and follow-up `27/11`.
- next: rerun with the rebuilt binary and see whether the added `0x1B/12` triggers `trace_business_handler pc=01010bc0 kind=27 subtype=12`, causes `trace_scene_send_type27_followup_request` to appear, or otherwise changes the stuck `loadFlags=1,0,0` state.

## 2026-06-03
- changed: added a read-only `trace_sub_1010228_entry` hook in `src/main.c` to log `LR`, source/destination object pointers, and the copied string when `sub_1010228()` runs.
- verified: `trace_loading_flags_change` now catches the first `0,0,0 -> 1,0,0` transition at `pc=0101029c`, which is the return from `sub_1010228()` after it stores `1` into `R9+0x5C74` and `R9+0x5C79`.
- verified: the same run still confirms the corrected scene-key path is local-scene-resource driven: `c00蓬莱仙岛_01 -> c00蓬莱仙岛_01.sce -> 00蓬莱仙岛_01.map -> m_peak1.gif / m_village05.gif / m_apotheosis.gif`.
- evidence: `bin/logs/net_trace.log` lines `21790-21791` and later `22901-22906`; IDA decompilation of `sub_1010228()` plus its only current callers `scene_runtime_init_and_sync()` (`0x01013594`) and `net_handle_group_info()` (`0x010124AA`); `bin/logs/storage_trace.log` lines `51750-51797`.
- hypothesis: `R9+0x5C79` may be a scene/group auxiliary-state byte rather than the whole loading-overlay root cause, because `sub_1010228()` is reused by `net_handle_group_info()` and mostly copies a `0x130`-byte side structure from `R9+0x5CA4` to `R9+0x5CF4`.
- next: rerun once with the rebuilt binary and inspect `trace_sub_1010228_entry` to confirm the live caller and the copied source string before deciding whether to keep chasing `0x5C79` or move the trace target to the actual `"正在加载中..."` draw predicate.

## 2026-06-02
- changed: added a read-only `trace_loading_flags_change` watcher in `src/main.c`; it caches the previous `loadFlags/loadMode/sceneObj` state and logs the first PC/LR whenever `Global_R9+0x5C79..0x5C7B` changes.
- verified: the newest clean rerun shows the remaining blocker has moved into a later local scene/runtime phase. `logs/storage_trace.log` confirms the scene-key path now resolves `c00蓬莱仙岛_01 -> c00蓬莱仙岛_01.sce -> 00蓬莱仙岛_01.map -> m_peak1.gif/m_village05.gif/m_apotheosis.gif`.
- verified: on the same run, `logs/net_trace.log` narrows the loading-flag transition window to after `tick=113` post-callback (`activeScreen=010534b4`, `loadFlags=0,0,0`) and before `tick=116` pre-business dispatch (`activeScreen=01053450`, `loadFlags=1,0,0`). After `kind=30 subtype=1`, `sceneGate` clears to `0` and `sceneState` becomes `7`, but `loadFlags` remain `1,0,0`.
- evidence: `bin/logs/net_trace.log` lines around `8908` and `10769`; `bin/logs/storage_trace.log` lines resolving `c00蓬莱仙岛_01`; no `trace_loading_flags` entries for `0x101A97C/0x101AED2/0x101D2D6/0x101D4B4/0x101EC62` appeared in this rerun.
- next: rerun once with the rebuilt binary and capture the first `trace_loading_flags_change` line to identify the exact writer that turns loading on during scene-screen construction/local scene load.

## 2026-06-03
- changed: switched the fresh-enter scene-list sequence fields `colnames`, `roomlist`, `npcinfo`, and `rolesinfo` from `vm_net_mock_put_object_blob()` to a raw object-entry helper in `src/main.c`, so those fields no longer carry an extra inner `u16 dataLen` wrapper.
- verified: the new list-header trace exposed a concrete encoding bug in the first `colnum/colnames` retry rather than a vague semantic mismatch.
- evidence: latest `bin/logs/net_trace.log` shows `trace_scene_colnames_item index=0 len=28`, exactly matching the whole `colnames` blob size, followed by `index=1 len=209 text=roomnum`; this is consistent with the parser treating the old inner `dataLen` prefix as the first string length and then walking off into later packet fields.
- next: rerun once with the raw-entry fix and confirm whether `trace_scene_colnames_item` now shows sane short names and whether `parse_scene_npcinfo_list()` returns instead of stopping the session at subtype `3`.

## 2026-06-03
- changed: added read-only scene-list tracepoints in `src/main.c` at `scene_parse_column_names()` entry (`0x01039180`), each copied column-name item (`0x010391E8`), and the return sites of `scene_parse_column_names()`, `parse_scene_npcinfo_list()`, and `sub_1039430()` (`0x010391F8`, `0x01039372`, `0x01039526`).
- verified: the first `colnum/colnames` retry changed behavior, but not in the hoped-for direction. The packet grows to `mock_group_type1_response ... len=477`, `kind=30 subtype=3` still lands on `parse_scene_npcinfo_list()`, and then the log stops there before any later `subtype=7`, follow-up scan, or `post_data_event` line.
- evidence: latest `bin/logs/net_trace.log` ends at `trace_business_handler pc=01039222 packet=05001508 kind=30 subtype=3 ... tick=250`; full-text search finds no later `trace_business_handler ... subtype=7` and no `trace_followup_scan_enter` in the same session, whereas the previous one-record/no-colnames run did reach `subtype=7`.
- next: rerun once with the new list-header trace and see whether the stall happens inside `scene_parse_column_names()` itself or only after it returns to `parse_scene_npcinfo_list()`.

## 2026-06-03
- changed: extended the fresh-enter `0x1E/3` / `0x1E/7` builders again so they now also send the list-header fields `colnum` and `colnames`, in addition to the one-record `roomlist` / `rolesinfo` test rows already in place.
- verified: static decompilation makes those header fields a concrete protocol requirement worth testing next. Both live consumers call `scene_parse_column_names()` before row parsing, and that helper explicitly reads `colnum` / `colnames` and stores up to four 10-byte column labels into the active room-list state.
- evidence: IDA for `scene_parse_column_names()` (`0x01039180`) shows reads of packet fields `colnum` and `colnames`; `parse_scene_npcinfo_list()` (`0x01039222`) and `sub_1039430()` (`0x01039430`) both call it before they read `roomnum/roomlist` or `rolenum/rolesinfo`. The current one-record rows already match the narrower row shape seen in IDA (`u32 + 3 strings` for rooms, `4 strings` for roles), so missing header metadata is now the sharpest remaining candidate.
- next: rerun once with the rebuilt binary and see whether adding `colnum/colnames` finally changes `sceneGate/sceneState/loadFlags`, or at least triggers a new downstream scene/business request.

## 2026-06-03
- changed: upgraded the fresh-enter `0x1E/3 roomlist` and `0x1E/7 rolesinfo` payloads from zero-count empty blobs to one-record test data so the parsing loops can actually execute.
- evidence: `parse_scene_npcinfo_list()` consumes one integer plus three strings per room entry, while `sub_1039430()` consumes four strings per role entry; the new test payloads now provide exactly one such record with `roomid=1001`.
- next: rerun the rebuilt binary and see whether the scene stall moves once those loops execute with non-empty content, or whether the next missing semantic shifts to another scene/business object.

## 2026-06-03
- changed: extended the fresh-enter experiment one more step so the built-in `type=1` reply now appends both `0x1E/3` and `0x1E/7`, and added a read-only handler trace for `sub_1039430()` (`0x01039430`).
- verified: `0x1E/7` now behaves the same way as `0x1E/3`: it definitely lands on the real consumer but still does not clear the stall. The latest run shows `mock_group_type1_response ... objects=5 sceneEnter=1 sceneRoomNpc=1 sceneRoomRoles=1 len=307`, then `trace_business_handler pc=01039b8a kind=30 subtype=7` and `trace_business_handler pc=01039430 kind=30 subtype=7`, with the same final `sceneGate=0 sceneState=7 loadFlags=1,0,0`.
- evidence: latest `bin/logs/net_trace.log` session tail shows the first post-login `event=7` consuming `kind=30 subtype=1`, `kind=30 subtype=3`, and `kind=30 subtype=7` in order, then `trace_followup_scan_enter objects=5`, and finally `runtime_state ... loadFlags=1,0,0 activeScreen=01053450`.
- next: stop adding more empty scene-channel wrappers; the next experiment should make `roomlist` and/or `rolesinfo` minimally non-empty so their parsing loops actually execute.

## 2026-06-03
- changed: extended the fresh-enter experiment in `src/main.c` again so the built-in `type=1` reply now appends both `0x1E/3` (`curpage/pagenum/roomnum/roomlist`) and a minimal `0x1E/7` (`roomid/rolenum/rolesinfo`) scene-channel object; also added a read-only handler trace for `sub_1039430()` (`0x01039430`).
- verified: the earlier `0x1E/3` experiment is now confirmed to land but still not clear the stall. The latest rerun shows `mock_group_type1_response ... sceneRoomNpc=1 len=259`, then `trace_business_handler pc=01039b8a kind=30 subtype=3` and `trace_business_handler pc=01039222 kind=30 subtype=3`, yet the callback still ends with `sceneGate=0 sceneState=7 loadFlags=1,0,0`.
- evidence: latest `bin/logs/net_trace.log` session tail shows `trace_business_handler pc=010114fc kind=10 subtype=26`, `pc=01039b8a kind=30 subtype=1`, then `pc=01039b8a kind=30 subtype=3` and `pc=01039222 kind=30 subtype=3` all inside the first post-login `event=7`; no `trace_scene_send_map_enter_request` appears, and no later scene packet clears `loadFlags`.
- next: rerun the rebuilt binary and check whether the new `0x1E/7` object lands (`trace_business_handler pc=01039430 kind=30 subtype=7`) and whether that finally changes the fresh-enter stall.

## 2026-06-03
- changed: appended a minimal fresh-enter `kind=0x1E subtype=3` scene-channel object in `src/main.c` after the built-in `type=1` reply, with fields `curpage=1`, `pagenum=1`, `roomnum=0`, `roomlist=""`, `npcnum=0`, and `npcinfo=""`.
- verified: the newest clean session does not go back through the earlier `scene_send_map_enter_request -> 0x1E/2` branch. It stops on the fresh-enter path after `type=1/2/3`, and only `trace_business_handler pc=01039b8a kind=30 subtype=1` appears.
- verified: the repeated `"正在加载中..."` draws in that session are all local scene-init refresh points, not a later persistent redraw loop. Every traced `sub_1003568()` call on the scene screen lands during `tick=118` inside `scene_runtime_init_and_sync()` / `scene_rebuild_runtime_nodes()`, before the queued `event=7` deliveries at `tick=119`; no later `lcd_text_call ... text=正在加载中...` lines appear after the `sceneGate=0 sceneState=7` callback sequence in the same session tail.
- evidence: latest `bin/logs/net_trace.log` shows no `trace_scene_send_map_enter_request`, no `kind=30 subtype=2`, no `kind=30 subtype=3`, and no `27/4`; instead it shows `mock_group_type1_response ... len=177`, then `trace_business_handler pc=01039b8a kind=30 subtype=1` at `tick=119`, with final `runtime_state ... sceneGate=0 sceneState=7 loadFlags=1,0,0`.
- next: rerun the rebuilt binary and check whether the new `0x1E/3` object is consumed (`trace_business_handler pc=01039222 kind=30 subtype=3`) and whether that changes the fresh-enter stall.

## 2026-06-02
- changed: added a tiny read-only `trace_status_tile_slots` watcher in `src/main.c` that logs the current `tileInfo[26]` / `tileInfo[65]` values and resolved resource pointers only when they change.
- verified: the existing draw-crash evidence is stable across reruns, so the next uncertainty is no longer "which tile first fails" but "whether those status-panel slots were ever initialized before first draw". The new watcher is meant to resolve that with one more reproduction.
- next: rerun once and inspect `trace_status_tile_slots` near scene init and first draw. If it never shows valid values, the gap is an initialization/seed contract; if it flips from valid to `-1`, the gap is a later clear/reset path.

## 2026-06-02
- verified: the first post-`ScreenInit Ok` crash is now pinned to the local status-panel tile table, not to networking. `trace_scene_runtime_tick` shows the client reaches normal draw pass at `0x01015158` and then `scene_draw_status_panels()` at `0x0101517A`; the next `trace_draw_map_tile_entry` is `tileId=26 x=0 y=0 w=240 h=32 repeat=3` from caller `0x010146A4`, with `tileIndex=-1` and `resPtr=0`.
- evidence: `logs/stdout_trace.log` records `地址无法访问:4 type:0 size:19 value:2` with `pc=0x0100E67C`, `lr=0x0100E8E3`, which IDA maps to `draw_image_tiled_clipped()` dereferencing a null image pointer from `draw_map_tile_by_index()`. IDA for `scene_draw_status_panels()` confirms `0x010146A4` is `draw_map_tile_by_index(26, 0, 0, 240, 32, 3u)`.
- evidence: `logs/storage_trace.log` in the same run still shows the normal scene-local resource chain (`c00蓬莱仙岛_01.sce -> 00蓬莱仙岛_01.map -> m_*.gif`), while the repo also contains `bin/JHOnlineData/UI2.gif`. That makes the missing `tileId=26` entry look like a map-manager/default-UI tile seeding gap rather than a missing map art file.
- next: inspect the map-manager population path for tile records `26` and `65`, and verify whether the unmodified game expects those status-panel tiles to be injected from a UI/default atlas instead of coming from the current `.map` payload.

## 2026-06-02
- changed: mirrored critical stdout diagnostics into `logs/stdout_trace.log` by adding `vm_stdout_trace()` in `src/main.c`, duplicating `ScreenInit Ok` / `ScreenResume Ok`, semihosting stdout, and fatal memory-access diagnostics from `hookRam.c`, while keeping terminal output intact.
- changed: added a read-only `trace_draw_map_tile_entry` hook at `draw_map_tile_by_index()` entry (`0x0100E8B0`) to log `tileId`, resolved `tileIndex`, map/tile table pointers, and final image resource pointer only when the resolved image pointer is null or the tile index is invalid.
- verified: the latest user-provided stdout failure is not a generic post-login hang. After `ScreenInit Ok`, the current live path can die with `地址无法访问:4 type:0 size:19 value:2`, `pc=0x0100E67C`, `lr=0x0100E8E3`, which IDA maps to `draw_image_tiled_clipped()` dereferencing `LDRH R1, [R0,#4]` with a null image pointer from `draw_map_tile_by_index()`.
- evidence: IDA decompilation shows `draw_map_tile_by_index()` resolves `tileId -> tileIndex -> image pointer` from the map manager at `R9+39664`, then calls `draw_image_tiled_clipped()`. Xrefs include `scene_draw_status_panels()` at `0x010146A4` and `0x010146B8` for tiles `26` and `65`, plus several `scene_runtime_tick()` draw sites.
- next: rerun once with the rebuilt binary and inspect `logs/stdout_trace.log` together with `logs/net_trace.log` for `trace_draw_map_tile_entry`. The immediate question is which `tileId/tileIndex/resPtr` pair is null on the failing path, so we can decide whether the remaining bug is a bad map tile table contract, a missing local image-resource slot, or a scene-status panel assumption resurfacing under the unmodified path.

## 2026-06-02
- changed: added a new read-only `trace_scene_runtime_tick` hook in `src/main.c` for `scene_runtime_tick` entry / draw-pass / status-panels / actor-pass (`0x01014D3A`, `0x01015158`, `0x0101517A`, `0x0101517E`), with compact logging of `sceneObj`, `sceneObj+1/+2`, `sceneTickGate`, and current `loadFlags`.
- verified: the latest clean run no longer ends in a post-login network/resource fault after the first fresh-enter callback cluster. After `tick=138` the client stays on `activeScreen=01053450`, `sceneGate=0`, `sceneState=7`, and local storage work continues through `c00蓬莱仙岛_01.sce`, `00蓬莱仙岛_01.map`, environment actor resources, and a long `item.dsh` scan.
- evidence: `bin/logs/net_trace.log` ends its current fresh-enter callback cluster at `tick=138` with `trace_followup_scan_enter` and `runtime_state ... sceneGate=0 sceneState=7 activeScreen=01053450`, but no later `host_signal`, `type=6`, or new outbound scene request; `bin/logs/storage_trace.log` shows successful local reads of the scene/map/actor family and no new `file_open_fail` beyond early optional boot files.
- next: rerun once with the new scene-tick trace enabled and check whether the path reaches `scene_runtime_tick` normal draw/world update after the first fresh-enter callback cluster, or whether it stalls earlier in a local scene-object mode/state before status/actor passes begin.

## 2026-06-03
- verified: the static chase around `scene_runtime_init_and_sync() -> sub_1045EA4()` did not reveal a second local preload branch for `tile25/26`. `scene_system_bootstrap()` only constructs the scene piclib and seeds its fixed filename table, while `scene_runtime_init_and_sync()` sets the status-panel dirty/visible byte before calling the only confirmed local tile preloader `sub_1045EA4()`.
- verified: the old note that the fixed piclib seed table only covered `0..46` was wrong. `sub_10434CE()` actually seeds names through `tileId=94`, including `tile65 -> UIsh7.gif`; the asymmetry is therefore not “65 came from a different table” but “65 is requested by `sub_1045EA4()` while `26` is never requested at all”.
- evidence: IDA decompilation of `scene_system_bootstrap()` (`0x01003856`) shows `sub_10439E4(*(_DWORD *)(R9+39664), 96)` constructing the piclib and `sub_10434CE()` seeding the filename table, but no per-tile `loadTile` calls before return. `scene_runtime_init_and_sync()` then sets `R9+0x5C64+0x15 = 1` at `0x01013780`, calls `sub_1045EA4()` at `0x010137B6`, and only afterwards enters a few deferred-popup/controller branches (`0x010137C6`, `0x01013808`, `0x01013836`, `0x01013864`). The helper object initialized by `sub_1048FEE()` is a callback-forwarder only and does not touch the scene piclib.
- next: stop expecting a hidden `25/26` preload right next to `sub_1045EA4()`. The next static/runtime target should be the broader first-use contract: either an earlier bootstrap path outside `scene_runtime_init_and_sync()` is supposed to seed `UI1/UI2`, or the current live path is enabling `scene_draw_status_panels()` too early by setting the status-panel dirty bit before that seed path has executed.

## 2026-06-03
- verified: the new per-tile piclib trace removes the remaining ambiguity around the crashy status-panel slots. In the latest clean run, `tile65` is actively requested through `sub_10433BA()` as `UIsh7.gif` and succeeds at `0x01043420`, but `tile26` never appears in `trace_piclib_slot_load` at all before `scene_draw_status_panels()` later tries to draw it and crashes with `tileIndex=-1`.
- verified: `scene_runtime_init_and_sync()` is now linked to a concrete hard-coded tile preloader. It calls `sub_1045EA4()` at `0x010137B6`, and that helper invokes the piclib `loadTile` method for a long fixed list that includes `59..65` and many other ids, but not `25` or `26`.
- evidence: newest `bin/logs/net_trace.log` shows `trace_piclib_slot_load label=piclib_slot_entry ... tileId=65 ... name=UIsh7.gif` followed by `trace_piclib_slot_load label=piclib_slot_load_success ... tileId=65 tileIndex=27`; no `tileId=26` piclib-load trace appears before the later `trace_draw_map_tile_entry ... tileId=26 ... tileIndex=-1`. IDA decompilation of `sub_1045EA4()` confirms the fixed preload list and shows the caller xref from `scene_runtime_init_and_sync()` at `0x010137B6`.
- next: stop treating this as a file-open failure for `UI2.gif`. The next RE step should identify which missing caller/state should request `tile25/26` before the first status-panel draw, or which alternative preloader branch the unmodified client is expected to take.

## 2026-06-03
- changed: added a new read-only `trace_piclib_slot_load` hook in `src/main.c` for the scene piclib per-tile loader path: `sub_10433BA()` entry (`0x010433BA`), local-open callsite (`0x010433DE`), success writeback (`0x01043420`), and remote/request fallback (`0x01043434`). The trace only logs `tileId=26/65`, together with the current `namePtr/name`, `loaded` flag, and `tileIndex`.
- verified: the latest static pass substantially narrows the status-panel tile crash. `scene_system_bootstrap()` calls `sub_10439E4(..., 96)`, and that function explicitly initializes every `tileInfo[i].tileIndex` to `0xFFFF` in the loop containing `0x01043BA6`; `0x01043420` is the success path in `sub_10433BA()` that writes a loaded resource slot back into `tileInfo[a2].tileIndex`.
- verified: `tileId=26` is statically mapped to `UI2.gif` in the fixed seed table built by `sub_10434CE()`. A later re-check corrected the table size too: it seeds names through `tileId=94`, including `tile65 -> UIsh7.gif`, even though the live crash still shows `tile26` never being requested.
- evidence: IDA decompilation for `scene_system_bootstrap()` (`0x01003950`) -> `sub_10439E4()` and `sub_10433BA()` in `江湖OL.CBE`; runtime watchers still show `trace_status_tile_slots ... tile26=-1 ... tile65=27 res65=050248f0` before the crash and then `trace_draw_map_tile_entry ... tileId=26 ... tileIndex=-1 resPtr=00000000`.
- next: rerun once with the new piclib-slot trace and capture whether `tile26` is never requested, requested as `UI2.gif` but fails local-open, or skipped because some later fill path never seeds its metadata at all.

## 2026-06-02
- changed: added more read-only scene/follow-up tracepoints in `src/main.c` for `scene_send_type27_followup_request()` (`0x0100ED20`), primary `net_handle_fb_target_dispatch()` (`0x01010BC0`), and `parse_scene_npcinfo_list()` (`0x01039222`).
- verified: the earlier ambiguity about appended follow-up objects is now gone. After the composite post-enter reply, the client definitely enters `net_handle_business_followup_events()` and consumes both `27/11` and `12/1`, but that still does not clear the loading overlay.
- evidence: latest `bin/logs/net_trace.log` shows `trace_followup_scan_enter objects=4 base=05001400` immediately after `trace_business_handler pc=01039b8a kind=30 subtype=2` and `pc=01011c88 kind=7 subtype=42`, then `trace_followup_case label=case27_11 ... kind=27 subtype=11` and `trace_followup_case label=case12_1 ... kind=12 subtype=1`; the following `runtime_state` still reports `loadFlags=1,0,0 activeScreen=01053450`.
- next: rerun with the rebuilt binary and check whether the client emits `trace_scene_send_type27_followup_request`, whether a later `kind=27 subtype=4` response path appears, or whether the next real gap is a scene-channel `0x1E/3` NPC/room list packet.

## 2026-06-02
- changed: added read-only follow-up tracepoints in `src/main.c` for `net_handle_business_followup_events()` entry (`0x01010594`) plus the currently most relevant case bodies `12/1`, `27/11`, and `27/4`.
- verified: the latest composite post-enter response already advances further than before, but still not all the way out of loading: the client consumes `kind=0x1E subtype=2` and `kind=7 subtype=42`, begins drawing the scene screen, and still leaves `loadFlags=1,0,0` with repeated `"正在加载中..."`.
- evidence: newest `bin/logs/net_trace.log` shows `mock_scene_change_combo_response objects=4 skill=1 fb11=1 len=156`, then at `tick=133` `trace_business_handler pc=01039b8a kind=30 subtype=2` and `trace_business_handler pc=01011c88 kind=7 subtype=42`, followed by `runtime_state ... sceneGate=0 sceneState=7 loadFlags=1,0,0 activeScreen=01053450`; no log line yet proves the appended `0x0C/1` or `0x1B/11` objects were scanned.
- next: rerun with the rebuilt binary and check whether `trace_followup_scan_enter` plus `trace_followup_case label=case12_1|case27_11|case27_4` appear after the composite scene-change reply; that will separate “objects not consumed at all” from “objects consumed but semantically incomplete”.

## 2026-06-02
- changed: extended the built-in map-enter response path so a composite request containing `maptype/mapID/exitID` now returns not only `kind=0x1E subtype=2 scene+posinfo`, but also a minimal empty `kind=0x1B subtype=11` object and the existing `kind=0x0C subtype=1` + `kind=7 subtype=42` learned-skill/books objects when those request objects are present in the same WT packet.
- verified: the previous experiment already advanced the client much further: the appended post-login `kind=0x1E subtype=1` object did fire `trace_business_handler pc=01039b8a kind=30 subtype=1`, cleared `sceneGate` to `0`, set `sceneState=7`, and triggered a real `trace_scene_send_map_enter_request maptype=2 ...`.
- evidence: latest `bin/logs/net_trace.log` shows `mock_group_type1_response ... objects=3 len=177`, then at `tick=235` both `pc=010114fc kind=10 subtype=26` and `pc=01039b8a kind=30 subtype=1`; later the client sends a new `len=86` WT packet containing object sequence `0x0C/1`, `0x07/42`, `0x02/3(maptype,mapID,exitID)`, `0x1B/11`, `0x0C/1`, `0x07/42`, but the emulator had only been responding with `0x1E/2`, after which `loadFlags` still remained `1,0,0`.
- next: rerun with the rebuilt binary and capture whether the added `0x1B/11` and `0x0C/1 + 0x07/42` responses finally clear the remaining loading state or expose the next exact post-enter protocol gap.

## 2026-06-02
- changed: extended the built-in post-login `type=1` composite reply in `src/main.c` with an additional `kind=0x1E subtype=1` object carrying `scene + posinfo`, while keeping the existing `groupinfo` and `kind=0x0A subtype=0x1A` name/money object intact.
- verified: the latest rerun confirms `type=2/3` now hit `net_handle_misc_player_fields()` (`0x01011C88`) for `pcimg/expcard`, but loading still stayed at `loadFlags=1,0,0`; this rules out the `type=2/3` route itself as the next missing semantic.
- evidence: `bin/logs/net_trace.log` newest tail shows one `trace_business_handler pc=010114fc kind=10 subtype=26` from the `len=129` packet, followed by `trace_business_handler pc=01011c88 kind=7 subtype=20` and `kind=7 subtype=32`, with `sceneGate` still `1`; IDA decompilation shows `parse_scene_response_entry()` (`0x010396D6`, dispatched from `kind=0x1E subtype=1`) is the scene-enter handler that consumes `scene + posinfo`, sets `sceneState=7`, and clears `sceneGate`.
- next: rerun with the rebuilt binary and capture whether the appended `0x1E/1` object produces a `trace_business_handler pc=01039b8a kind=30 subtype=1`, clears `sceneGate`, or triggers any later `scene_send_map_enter_request` / scene runtime path.

## 2026-06-02
- changed: reverted `src/main.c` built-in post-login `type=2/3` replies from the experimental `kind=0x0A subtype=0x1A scene+posinfo` shape back to `kind=7 subtype=20/32` carrying `pcimg` / `expcard`, while keeping the new read-only business-handler tracepoints enabled.
- verified: the latest trace proved the `0x0A/0x1A` experiment was routed into `net_handle_role_login_gift_glamour()` (`0x010114FC`) three times and never touched the real scene handlers or `pcimg/expcard` path, so leaving that experiment in place would only keep feeding the wrong handler.
- evidence: `bin/logs/net_trace.log` shows `trace_business_handler pc=010114fc ... kind=10 subtype=26` for all three post-login callbacks at `tick=129`, with `loadFlags` staying `1,0,0`; IDA shows `net_handle_role_login_gift_glamour()` only consumes fields such as `name`, `money`, `giftinfo`, and `glamour`, while `net_handle_misc_player_fields()` owns `pcimg` / `expcard`.
- next: rerun with the rebuilt binary and confirm whether `type=2/3` now hit `net_handle_misc_player_fields()` (`0x01011C88`) and whether that changes the loading stall or exposes the next missing post-login packet semantic.

## 2026-06-02
- changed: added read-only tracepoints in `src/main.c` for `scene_send_map_enter_request()` and the nearby business handlers `net_handle_misc_player_fields`, `net_handle_role_login_gift_glamour`, `net_handle_scene_posinfo_response`, and `net_handle_scene_channel_dispatch`.
- verified: switching `type=2/3` built-in replies to `kind=0x0A subtype=0x1A` with `scene+posinfo` did change the payloads (`len=75` each), but it still did not clear loading; the newest run stayed on `activeScreen=01053f78` with `loadFlags=1,0,0`.
- evidence: `logs/net_trace.log` shows the new `type=2/3` replies as `header=01,07,0a,1a` with `scene` and `posinfo`, followed by three `event=7` callbacks where `runtime_state` remains unchanged; IDA confirms business kind `0x0A` dispatches to `net_handle_role_login_gift_glamour()`, while real scene-position updates are handled through `0x1C` / `0x1E`.
- next: rerun once with the new tracepoints to determine whether the client ever sends/enters the real scene-enter path (`scene_send_map_enter_request`) or whether the current post-login reply chain never reaches that state.

## 2026-06-02
- changed: redirected runtime traces to `logs/net_trace.log` and `logs/storage_trace.log` relative to the emulator working directory, creating `logs/` lazily on first write so future reproductions stay grouped under a dedicated log folder.
- changed: switched built-in post-login `type=2/3` game-entry replies in `src/main.c` back to the older documented `scene + posinfo` shape under header `1,7,0x0A,0x1A`, reusing `vm_net_mock_put_scene_fields()` instead of the later minimalist `kind=7 subtype=20/32` `pcimg/expcard` replies.
- verified: on the latest post-revert run before this patch, the newest log no longer ended in a crash after login. The client entered loading screen `01053f78`, consumed login (`len=443`) plus `type=1/2/3` replies (`129/34/36`), and then remained in loading with `loadFlags=1,0,0`.
- evidence: `bin/net_trace.log` newest tail shows `screen_manager idx=4 add r0=01053f78`, then `mock_login_actor_response`, later `mock_group_type1_response`, `mock_game_type_response type=2`, `mock_game_type_response type=3`, and `runtime_state ... loadFlags=1,0,0 activeScreen=01053f78`; `docs/re/protocol.md` and `docs/net_mock_protocol.md` both already record that `type=2/3` game-entry responses need subtype `0x1A` with `scene` and `posinfo` to progress.
- next: rerun with the rebuilt binary and confirm whether the restored `scene + posinfo` semantics clear the loading screen or at least move the failure point forward into later scene runtime code.

## 2026-06-02
- changed: added a minimal `vmMfMemoryBlockManager` bridge in `src/runtime/emulator_runtime.c` so slots `0/1/2` now call the existing `vm_MF_MemoryBlock_Malloc()`, `vm_MF_MemoryBlock_Reset()`, and `vm_MF_MemoryBlock_Release()` helpers instead of being uniformly stubbed.
- verified: after the earlier `vmDfDataPackageManager idx=0/4` fixes, the client now gets far enough to show the loading/version UI and only aborts later at `tick=66` when queued network event `5` fires into callback `0x0103489B`; the last guest PC before the abort is `0x01034524` inside the event-packet field initializer `sub_1034518`.
- evidence: fresh `bin/net_trace.log` shows `vmDfDataPackageManager_impl idx=4` followed by normal UI drawing, then `open_channel ... cb=0103489b`, delayed dispatch of event `5`, `fire_event slot=0 event=5`, and finally `host_signal sig=22 ... last=01034524`; IDA shows `sub_1034518` is used by `event_packet_add_field` / `event_packet_parse_WT`, and `src/vmFunc.c:1919` documents `VM_MF_MemoryBlock_FUNC_LIST_ADDRESS` slot `0/1/2` as the memory-block `Malloc/Reset/Release` entrypoints.
- next: rerun the rebuilt binary to confirm whether satisfying `vmMfMemoryBlockManager` slot `0/1/2` allows the version-request/event-packet path to continue, or whether another manager/interface becomes the next confirmed blocker.

## 2026-06-02
- changed: reconnected `vmDfDataPackageManager idx=0` to `vm_DF_DataPackage_LoadPackage()` and `idx=4` to `VM_DF_DataPackage_DoLoading()` in `src/runtime/emulator_runtime.c`, using the existing implementations in `src/vmFunc.c` instead of stubbing or forcing the client forward.
- verified: on the unmodified runtime, the first true post-login blocker was earlier than the old scene/status trace: `idx=0` was hit and aborting at `tick=1`; after fixing that, the next exposed missing entry is `idx=4`, matching the DF_DataPackage manager layout already documented in `vm_initDFDataPackage()`.
- evidence: fresh `bin/net_trace.log` lines show `vmDfDataPackageManager_impl idx=0` from the `0x0103B9FC` resource-init path where earlier runs aborted, and the user-observed runtime print changed to `[impl]vmDfDataPackageManager调用位置:4`; `src/vmFunc.c` defines `VM_DF_DataPackage_DoLoading()` at the corresponding slot.
- next: rerun the rebuilt binary and capture whether another DF_DataPackage slot is missing next, or whether control finally moves on to the later loading/scene path.

## 2026-06-02
- changed: removed the earlier progress-forcing scene hook from `src/runtime/emulator_runtime.c` that backfilled scene tile entries `25/26` from fallback tiles and short-circuited missing tile draws by returning success to the client draw path.
- verified: rebuilt successfully with `make` after removing that in-game-state mutation hook; the runtime now exposes the post-login path without those tile-table rewrites and draw skips.
- evidence: deleted `vm_runtime_patch_bootstrap_tiles` / `vm_runtime_skip_missing_tile_draw` and their call sites; `make` linked a new `bin/main.exe`.
- next: reproduce the login-success path again on the unmodified runtime and treat earlier status-panel/divide findings as historical evidence from the previously modified run until reconfirmed.

## 2026-06-02
- changed: narrowed the `Post-Login Loading Crash` with existing `bin/net_trace.log`, IDA call chains, and a new minimal post-preload runtime trace at the two `scene_draw_status_panels` divide sites (`0x0101477E`, `0x010147AE`) to capture the next failing divisor/value pair.
- verified: the post-login path already gets past the title/login module and into main-scene initialization; the client sends follow-up `type=1/2/3` game requests, receives the current built-in mock responses, reaches `scene_runtime_tick` / `scene_draw_status_panels`, and then aborts through the C runtime divide/error path instead of failing earlier in screen transition.
- evidence: `bin/net_trace.log` shows `mock_group_type1_response` plus `mock_game_type_response type=2/3`, then `post_preload_status_draw`, valid tile states for `25/26`, and finally `host_signal sig=22` with a crash ring that runs from `scene_draw_status_panels` into `sub_104D538 -> sub_104E7C8 -> sub_104EA2C`; IDA confirms `sub_104D538` is the signed divide helper that aborts on zero divisor.
- next: rerun with the new divisor trace enabled, identify which status field is zero at the failing divide, and decide whether the fix belongs in the mock scene/type-1 payload or in local scene/status initialization semantics.

## 2026-06-02
- changed: documented the current reverse-engineering operating context for future sessions, including the main IDA analysis entry points, the project goals, current protocol/emulator progress, and the active blocker after login success.
- verified: the project currently has successful version-check and account-login mock responses, but still crashes after the login screen transitions into the loading stage.
- evidence: user-provided project status and workflow notes; current durable references now live in `docs/re/README.md`, `docs/re/firmware-map.md`, and `docs/re/open-questions.md`.
- next: capture the post-login crash context, determine whether the fault is caused by a missing emulator interface or missing follow-up packet flow, and record the confirmed cause once reproduced.

## 2026-06-02
- changed: split the emulator entry/assembly flow out of `src/main.c`; bootstrapping now stays in `src/main.c`, shared emulator globals moved to `src/core/emulator_state.c`, and the large runtime/hook implementation moved to `src/runtime/emulator_runtime.c`.
- verified: rebuilt successfully with `make` after converting `vmFunc.c`, `hookRam.c`, and `vmEvent.c` from `main.c` inclusions into normal object files in `Makefile`.
- evidence: `make` completed and linked `bin/main.exe`; `src/main.c` is reduced to 175 lines while the extracted runtime module carries the prior emulator/runtime behavior.
- next: continue peeling `src/runtime/emulator_runtime.c` by subsystem, with screen/input/network manager hooks as the next clean split points.

## 2026-06-02
- changed: extracted a new runtime service layer into `src/runtime/emulator_services.c` plus `src/runtime/emulator_runtime_internal.h`; LCD helpers, input overlay/event handling, SDL event loop, and NV/profile persistence no longer live in `src/runtime/emulator_runtime.c`.
- verified: rebuilt successfully with `make` after wiring the new runtime service object into `Makefile` and switching the remaining runtime code to the shared internal header.
- evidence: `src/runtime/emulator_services.c` now holds 609 lines of service code; `src/runtime/emulator_runtime.c` dropped to 8789 lines and still links into `bin/main.exe`.
- next: split the remaining `vm manager` dispatch block out of `src/runtime/emulator_runtime.c`, likely into one or more manager-focused modules using the new internal runtime header.

## 2026-06-02
- changed: pulled the top-level `vm manager` dispatch table (`vMInit*` / `vMGet*`) into `src/runtime/emulator_managers.c`, leaving `src/runtime/emulator_runtime.c` focused on concrete manager bodies plus the wider hook orchestration.
- verified: rebuilt successfully with `make` after wiring `obj/runtime/emulator_managers.o` into `Makefile` and exposing the shared manager callback through `src/runtime/emulator_runtime_internal.h`.
- evidence: `src/runtime/emulator_managers.c` now holds 361 lines for the top-level manager dispatch shim while `src/runtime/emulator_runtime.c` remains buildable at 8908 lines and still links into `bin/main.exe`.
- next: continue peeling the concrete sub-manager dispatchers out of `src/runtime/emulator_runtime.c` in groups such as LCD, screen, and network.

## 2026-06-02
- changed: split the concrete LCD, screen, and network vm manager bodies into `src/runtime/emulator_manager_lcd.c`, `src/runtime/emulator_manager_screen.c`, and `src/runtime/emulator_manager_network.c`; `src/runtime/emulator_runtime.c` now keeps only thin dispatch shims plus the shared runtime helpers/state those modules still call.
- verified: rebuilt successfully with `make` after adding the three new manager objects to `Makefile` and widening the internal runtime header for the shared helper/state boundary.
- evidence: `src/runtime/emulator_runtime.c` dropped to 7793 lines, while the extracted manager modules now carry 868 lines for LCD, 126 lines for screen, and 118 lines for network, and the link step still produced `bin/main.exe`.
- next: keep peeling the remaining concrete manager families out of `src/runtime/emulator_runtime.c`, with `fileio/stdout/timer/ctrl` or the DF/game utility clusters as the next clean candidates.

## 2026-06-02
- changed: split the `fileio`, `stdio`, `timer`, and `ctrl` vm manager bodies into `src/runtime/emulator_manager_fileio.c`, `src/runtime/emulator_manager_stdio.c`, `src/runtime/emulator_manager_timer.c`, and `src/runtime/emulator_manager_ctrl.c`; `src/runtime/emulator_runtime.c` now dispatches those manager families through thin wrappers.
- verified: rebuilt successfully with `make` after adding the four new manager objects to `Makefile` and exporting the timer scheduler helpers through `src/runtime/emulator_runtime_internal.h`.
- evidence: `src/runtime/emulator_runtime.c` dropped to 7301 lines, while the extracted manager modules now carry 194 lines for file I/O, 230 lines for stdio, 71 lines for timer, and 18 lines for ctrl, and the link step still produced `bin/main.exe`.
- next: continue peeling the higher-churn manager clusters out of `src/runtime/emulator_runtime.c`, with `df_engine / df_script / game_util / gameold` or the media-oriented groups as the next clean split.

## 2026-06-02
- changed: split the `game_util`, `df_engine`, `df_script`, and `gameold` vm manager bodies into `src/runtime/emulator_manager_game_util.c`, `src/runtime/emulator_manager_df_engine.c`, `src/runtime/emulator_manager_df_script.c`, and `src/runtime/emulator_manager_gameold.c`; `src/runtime/emulator_runtime.c` now dispatches those manager families through thin wrappers and keeps the download/video manager stubs local as shared runtime fallbacks.
- verified: rebuilt successfully with `make` after adding the four new manager objects to `Makefile` and exporting the shared DreamFactory helper lookups through `src/runtime/emulator_runtime_internal.h`.
- evidence: `src/runtime/emulator_runtime.c` dropped to 6005 lines, while the extracted manager modules now carry 204 lines for game util, 34 lines for df engine, 25 lines for df script, and 457 lines for gameold, and the link step still produced `bin/main.exe`.
- next: continue peeling the remaining media/download-oriented runtime handlers out of `src/runtime/emulator_runtime.c`, especially `game_lcd`, `audio`, `appstore`, and the dl/video helper families that are still grouped in the core runtime file.

## 2026-06-02
- changed: split the remaining media/download-oriented handlers into `src/runtime/emulator_manager_game_lcd.c`, `src/runtime/emulator_manager_audio.c`, `src/runtime/emulator_manager_appstore.c`, `src/runtime/emulator_manager_dl.c`, and `src/runtime/emulator_manager_video.c`; `src/runtime/emulator_runtime.c` now routes `game_lcd`, `audio`, `appstore`, `dl_*`, and `video` through thin wrappers, while the shared env-flag helper `vm_net_mock_env_u32` is exposed through `src/runtime/emulator_runtime_internal.h`.
- verified: rebuilt successfully with `make` after wiring the five new manager objects into `Makefile` and moving the video-manager log state into its dedicated module.
- evidence: `src/runtime/emulator_runtime.c` dropped to 5759 lines, while the extracted manager modules now carry 42 lines for game lcd, 178 lines for audio, 29 lines for appstore, 55 lines for dl, and 45 lines for video, and the link step still produced `bin/main.exe`.
- next: continue peeling the remaining runtime-local manager families such as `netapp`, `sensor`, and `vmim`, or start a second pass that groups shared download/network stub helpers into a smaller `runtime_support` layer if you want the core runtime file to keep shrinking.

## 2026-06-02
- changed: split the remaining `netapp`, `sensor`, and `vmim` manager bodies into `src/runtime/emulator_manager_netapp.c`, `src/runtime/emulator_manager_sensor.c`, and `src/runtime/emulator_manager_vmim.c`; `src/runtime/emulator_runtime.c` now dispatches those three families through thin wrappers, and the sensor-manager log state moved into its dedicated module.
- verified: rebuilt successfully with `make` after wiring the three new manager objects into `Makefile` and extending `src/runtime/emulator_runtime_internal.h` with the new manager entry declarations.
- evidence: `src/runtime/emulator_runtime.c` dropped to 5645 lines, while the extracted manager modules now carry 35 lines for netapp, 43 lines for sensor, and 57 lines for vmim, and the link step still produced `bin/main.exe`.
- next: the remaining large work in `src/runtime/emulator_runtime.c` is now much more runtime-core oriented; the next useful split is likely a second pass on shared support code such as manager stub helpers, scheduler/network support, or post-preload tracing support.

## 2026-06-03
- changed: completed the startup/update screen lifecycle cleanup in the `江湖OL.CBE` IDB by renaming `0x0103ADA4 -> startup_screen_update_handle_input`, `0x0103AE4A -> startup_screen_update_destroy`, `0x0103ADF8 -> startup_screen_update_handle_softkeys`, `0x0103AD34 -> startup_screen_update_handle_bottom_buttons_touch`, `0x01039DAC -> startup_screen_update_ensure_state10_mid_loaded`, and `0x01039D7C -> startup_screen_load_mid_resource_if_needed`, plus the paired left/right actions `0x0103ACEE` / `0x0103ACA4`.
- verified: `startup_screen_update_bind_callbacks()` now reads as a complete screen-lifecycle table: init, destroy, input/logic, draw, pause(no-op), and an extra state-10 hook. The input path cleanly splits into softkey dispatch (`4096` left, `0x2000` right) and bottom-button hit-testing at `y=368`. The state-10 extra hook lazily loads `MMORPG_Resource_Data_%d.mid` once and caches the handle into the screen-local subobject.
- evidence: decompile/xref inspection at `0x0103B1DE`, `0x0103ADA4`, `0x0103ADF8`, `0x0103AD34`, `0x0103AE4A`, `0x01039DAC`, and `0x01039D7C`; the `.mid` filename is built inline by `fmt_sprintf_like("MMORPG_Resource_Data_%d.mid", ...)`.
- next: if needed, continue into `startup_screen_update_draw()` state cases `0/1/2/8/9/10/11` and rename the remaining mode-specific helpers so the full startup/update state machine reads end-to-end in business terms.

## 2026-06-03
- changed: translated the `startup_screen_update_draw()` state machine and its install/preparation helpers into business terms by renaming `0x0103A84A -> startup_screen_draw_postinstall_progress`, `0x0103A8E4 -> startup_screen_draw_music_prompt`, `0x0103A282 -> startup_screen_install_mid_chunk_step`, `0x0103A02E -> startup_screen_open_install_mid_chunk`, `0x0103A01E -> startup_screen_open_updatetempbin`, `0x0103A6B8 -> startup_screen_prepare_game_data_step`, `0x0103A660 -> startup_screen_extract_one_game_data_entry_to_temp`, `0x0103A55C -> startup_screen_commit_temp_data_file_into_game_data`, `0x0103A540 -> startup_screen_close_game_data_temp_handle`, `0x0103A498 -> startup_screen_open_updateversion_cbm`, and `0x0103A4F6 -> startup_screen_send_start_client_event`.
- verified: the `startup_screen_update_draw()` states now map cleanly to screen pages: `0/7 = 获取版本信息 / 正在和服务器通讯请稍候....`, `1 = 正在更新配置文件 + 下载进度%d/%d`, `2 = 正在更新程序文件 + 下载进度%d/%d`, `8 = 游戏数据准备中，请耐心等待...`, `9 = post-install transition progress bar`, `10 = 游戏正在安装%d%%` with one `MMORPG_Resource_Data_%d.mid` install step per frame, and `11 = 是否开启音乐?` prompt with yes/no actions.
- evidence: GBK string table near `0x0103AE9C` (`获取版本信息`, `版本升级`, `正在更新配置文件`, `正在更新程序文件`, `游戏下载数据准备中...`, `游戏正在安装%d%%`) plus prompt strings near `0x0103AA7C` (`是否开启音乐?`, `可在系统设置中开启或关闭音乐`, `否`, `是`), combined with decompile flow at `0x0103A986`, `0x0103A282`, and `0x0103A6B8`.
- next: if needed, continue into the remaining state-0/1/2/8 transition setters and the `mmorpg_updateversioncbm` / `mmorpg_updatetempbin` follow-up chain to make the startup/update downloader state graph fully explicit.

## 2026-06-03
- changed: completed the next `updateversioncbm` follow-up layer in the `江湖OL.CBE` IDB by renaming `0x0103B59A -> startup_handle_update_target_metadata`, `0x0103B860 -> startup_handle_update_data_chunk`, `0x0103B45A -> startup_screen_request_next_update_chunk`, `0x0103B4AA -> startup_screen_select_phase_and_continue`, `0x0103B264 -> startup_screen_is_update_target_already_current`, `0x0103B4D4 -> startup_screen_alloc_update_data_buffer`, and `0x0103B69C -> startup_screen_append_update_data_chunk_to_temp`.
- verified: the updater follow-up chain now reads as: parse update target metadata (`type/id/code`), compare it with the remembered local target/version pair, optionally download/commit `mmorpg_updatetemp`, stream `data` chunks into the 50 KB temp buffer/file, request more chunks through event `(6,1,18,8)` while `start/version/screen` advance, then either enter startup state `8` for local game-data preparation or mark the deferred client-start path.
- evidence: decompile flow at `0x0103B59A`, `0x0103B860`, `0x0103B45A`, and `0x0103B69C`; packet fields/keys observed inline include `type`, `id`, `code`, `totalsize`, `totalnum`, `version`, `data`, `screen`, and `start`.
- next: if needed, continue one level lower into the parser/dispatch that calls `startup_handle_update_target_metadata()` and `startup_handle_update_data_chunk()` so the full startup downloader network protocol is explicit from incoming packet to final `start client` handoff.

## 2026-06-03
- changed: tightened the scene-HUD allocation annotations around `scene_runtime_init_and_sync()` for `0x01013122 / 0x01013138 / 0x01013142 / 0x0101315A`.
- verified: `0x01013122` allocates a six-entry bank of `44-byte` portrait actor widgets, not tile-strip records; one of those entries is later selected at `0x010136DE` and drawn through its `+0x18` callback at `0x010146DA` as the left portrait/status block. `0x0101315A` allocates a seven-entry bank of `44-byte` helper actor widgets that later bind `fuhao.actor` plus related teleport/transfer helpers through the shared actor-asset slot-table path. `0x01013138` and `0x01013142` are paired `0x694-byte` HUD panel/controller instances from the same base family, with the first one clearly running through `scene_hud_main_panel_init()`.
- evidence: line-level decompile comments and consumer sites at `0x010136DE`, `0x010146DA`, and `0x01013706`, plus the panel-base initializer call in `0x010352AE -> scene_hud_main_panel_init`.
- next: if needed, keep following the `0x694-byte` panel family to identify the exact downstream role of the secondary panel object allocated at `0x01013142`.

## 2026-06-03
- changed: annotated the tile-26 lookup/load path behind `scene_draw_status_panels()` and `draw_map_tile_by_index(26, 0, 0, 240, 32, 3)`.
- verified: tile id `26` maps to `UI2.gif` inside `0x010434CE -> scene_ui_tile_catalog_build_filename_table` (string slot at `0x01043768`). The only shared scene-tile loader is `0x010433BA -> scene_ui_tile_catalog_load_tile`, but the normal scene runtime preload `0x01045EA4 -> scene_ui_tile_catalog_preload_runtime_tiles` does **not** request tile `26`; it loads `24`, `27`, `28`, `59..65`, `46..49`, and other HUD tiles, but skips `25/26`. This means the `UI2.gif` load in `0x0103B0B0 startup_screen_update_init()` is unrelated screen-local art and does not populate the scene tile slot used by `draw_map_tile_by_index(26, ...)`.
- evidence: decompile at `0x0100E8B0 -> draw_map_tile_by_index`, `0x010433BA -> scene_ui_tile_catalog_load_tile`, `0x01045EA4 -> scene_ui_tile_catalog_preload_runtime_tiles`, and the `UI2.gif` filename entry in `scene_ui_tile_catalog_build_filename_table`.
- next: if needed, trace alternate/rare scene-entry paths to see whether any nonstandard loader path ever calls `scene_ui_tile_catalog_load_tile(catalog, 26)` before the first HUD draw; current static evidence says the normal init path does not.

## 2026-06-03
- changed: cleaned up the `mmTitleMstarWqvga.cbm` login-response chain by keeping `0x0016DC -> net_handle_login_response` as the main parser and renaming its surrounding helpers: `0x002D80 -> login_primary_response_dispatch`, `0x002A50 -> login_alt_response_dispatch_wrapper`, `0x001430 -> login_record_save_default_credentials`, `0x0014F4 -> login_response_parse_server_color_table`, and `0x00160A -> login_response_preload_title_images`.
- verified: the login reply parser now reads as: parse `result`, optionally reset the staged result buffer for subcmd `12`, consume `serverinfo/servernum/newVer`, populate the local server-selection tables, parse optional `color` metadata and `titleimgs`, and for result codes `3/4` copy `information/username/password` back into the login-record buffers for the next UI flow. The primary wrapper (`type=1, subcmd=1`) optionally persists the default-account record before calling `login_response_result_dispatch()`.
- evidence: decompile and string keys at `0x0016DC`, `0x002D80`, `0x002A50`, `0x0014F4`, and `0x00160A`; packet fields observed inline include `result`, `serverinfo`, `servernum`, `newVer`, `information`, `username`, `password`, `color`, and `titleimgs`.
- next: if needed, continue from the success callbacks at `r9+10464`, `r9+10800`, and `r9+10804` to pin down the first post-login screen init target inside the title module/runtime handoff.

## 2026-06-04
- changed: hardened the title-login runtime watcher so `trace_title_login_state` can recover the active title module base from the emulator's screen stack even after `scheduler_prepare_screen_call()` clears `g_currentScreenModuleBase`; the `pre/post_data_event` trace gate now keys off “active screen has a recoverable module base” instead of the earlier wrong `taskCallback == 0x05017509` assumption.
- verified: the previous “no `trace_title_login_state` output” was caused by emulator-side bookkeeping, not by the absence of local title state. Current logs already show `screen_manager idx=4 add ... moduleBase=01050bd0`, but by the time the `event=7` login replies fire the runtime-state snapshot had fallen back to `module=00000000`; the new watcher path now explicitly falls back to the screen-stack copy of that `01050bd0` module base.
- verified: `0x0103489A -> net_wrapper_event_dispatch` does not call the title/business callback directly as the queued task callback. It first invokes the net-manager callback at `get_net_manager_object()+0x44`, then conditionally invokes the current business object callback at `*(R9+0x94A8)+0x14`. In the current “click confirm, no UI transition” logs, `net_state_observe cb44=01037473`, and static RE confirms `0x01037472 -> handle_version_update_response`; this means the `event=7` wrapper is still entering the version/update family before any title-local business callback can matter.
- evidence: runtime logs `screen_manager idx=4 add r0=010534b4 ... moduleBase=01050bd0`, repeated `runtime_state ... activeScreen=010534b4 currentThis=0105349c module=00000000`, and `net_state_observe cb44=01037473`; static decompile at `0x0103489A` (`net_wrapper_event_dispatch`) and `0x01037472` (`handle_version_update_response`).
- hypothesis: the current active title screen `010534b4` is not the `0x002D80 -> login_primary_response_dispatch` owner. Its local business callback still needs to be pinned down from the title module side, but the wrapper-level control flow already explains why the `0x23C0` screen-switch BLX sites never fired in the latest runs.
- next: rerun once with the repaired `trace_title_login_state` watcher, then compare `pre/post_data_event` local title fields against the still-stationary `activeScreen=010534b4` path to see whether the reply mutates mode/target objects at all.

## 2026-06-04
- verified: the repaired `trace_title_login_state` watcher now fires on the stuck login path and confirms that the `1/1/1` primary-validation reply plus the staged `1/1/16 -> 1/1/4` follow-up do not mutate the active title screen's local state at all. Across both `fire_event slot=0` and `slot=1`, `activeScreen=010534b4`, `base=01050bd0`, `mode=0`, `sel=0,0`, `target48/4c/50/54` remain stable, `listPtr` stays `0`, and `roleFamily` stays `0`.
- verified: the only local state flip visible around the login screen before those packets is `state0: 0 -> 1` and `saveDefault: 0 -> 1`; that transition happens before the login replies at tick `220` and is therefore attributable to prior local UI activity, not to the `1/1/1` response itself.
- evidence: `bin/logs/net_trace.log` lines with `trace_title_login_state label=pre_data_event_05017509` and `post_data_event_05017509` at ticks `220` and `252`, plus repeated `net_state_observe cb44=01037473`.
- conclusion: current mock login success is being accepted at the transport/task level, but it is not driving the `010534b4` title-local state machine toward server-list or role-list mode. This is stronger than the earlier “no screen switch” result: the active screen's own mode/list/target fields remain untouched by the response.
- next: stop treating `1/1/1` payload tweaks as the likely blocker and instead identify which title-local parser/dispatcher actually owns `010534b4`'s `mode/listPtr/roleFamily` fields, or which earlier net-manager state change is required before the title business callback can consume the login success.

## 2026-06-04
- changed: added a low-noise write watcher for the active title-login local-state fields (`state0`, `focus`, `saveDefault`, `mode`, `sel0/1`, `listPtr`, `target48/4c/50/54`, `roleFamily`). The watcher hooks Unicorn `UC_MEM_WRITE` through `src/hookRam.c` and logs only writes that overlap the currently active title module base recovered from the screen stack.
- verified: build completed with `make` after wiring the watcher through `vm_net_trace_title_login_write()` in `src/main.c`.
- purpose: the earlier state snapshots proved that login replies leave `010534b4` unchanged, but they could not show whether anything tried and failed to update those fields between snapshots. The new write trace will let the next run attribute each state transition to a concrete guest write address.

## 2026-06-04
- verified: the write watcher now confirms that the stuck login path has no response-driven title-local writes at all. During the current `010534b4` screen lifetime, the only observed writes into the watched state block are:
  - `focus <- 0` at `last=050192ee`, tick `70`
  - `saveDefault <- 1` at `last=050196ca`, tick `237`
  - `state0 <- 1` at `last=050196cc`, tick `237`
- verified: no `trace_title_login_write` entries occur during the queued login reply events at ticks `220/252/288`, even though `mock_login_primary_validation_response` and `mock_title_role_stage4_response` are both delivered and `trace_title_login_state` fires before/after them.
- evidence: `bin/logs/net_trace.log` lines `trace_title_login_write label=focus ... last=050192ee`, `trace_title_login_write label=saveDefault ... last=050196ca`, `trace_title_login_write label=state0 ... last=050196cc`, followed by unchanged `trace_title_login_state` snapshots across `fire_event slot=0/1`.
- conclusion: current `1/1/1` handling is not merely “failing to switch screen”; it is not even attempting to mutate the active `010534b4` screen's `mode/listPtr/roleFamily/target*` state. The visible login-button path only toggles local `state0/saveDefault`, then the network replies are consumed elsewhere without reaching this screen-local state machine.
- next: map the local writers at `0x050192EE / 0x050196CA / 0x050196CC` back into `mmTitleMstarWqvga.cbm` and identify the specific `010534b4` input/state handlers they belong to; that should expose which local mode gate must be satisfied before any post-login network response becomes relevant.

## 2026-06-04
- verified: the `saveDefault/state0` writes seen on button press map back to `mmTitleMstarWqvga.cbm:0x2620 -> login_record_init_and_load()`, not to any network-response parser. Static decompile shows this routine clears/loads `mmorpg_LoginRecord`, initializes IMSI, copies persisted username/password into the active login buffers, and writes the local login-state bytes inside the `R9+0x29E8` block.
- verified: the same `R9+0x29E8` state block has a more relevant inbound parser family at `0x53EC`. That parser handles top-level `type=1` with subtypes `6/7/8/15`:
  - subtype `7` -> `sub_5324()`, which appends entries into `*(R9+10788)` and updates list-count related local state
  - subtype `15` with `result==1` -> `sub_39A4()`, which sets `*(R9+10748)=1`
- verified: `sub_39A4()` is the same local mode transition previously identified as the role-selection gate (`mode <- 1`).
- evidence: static decompile at `0x2620`, `0x53EC`, `0x5324`, and `0x39A4`; runtime write traces at `last=050196ca/050196cc`.
- conclusion: the current mock replies (`1/1/1` primary success plus staged `1/1/16 -> 1/1/4`) are aimed at the wrong title-local response family for the active `R9+0x29E8` state machine. The screen-local logic that owns `mode/listPtr` is much more consistent with a `type=1, subtype=7/15` follow-up family than with the earlier `1/1/4` role-list hypothesis.

## 2026-06-04
- changed: switched the default staged login follow-up experiment from the old `1/1/16 -> 1/1/4` role-stage packet to a minimal `1/1/15 {result=1}` packet.
- verified: rebuilt successfully with `make` after replacing `mock_title_role_stage4_response` with `mock_title_mode15_response` in `src/main.c`.
- purpose: isolate the narrowest confirmed local mode gate first. Static RE already shows `type=1, subtype=15, result=1 -> sub_39A4() -> *(R9+10748)=1`, so this experiment is a cleaner validation target than the earlier mixed role-list hypothesis.

## 2026-06-04
- verified: the minimal `1/1/15 {result=1}` follow-up does get queued and delivered on the current login path (`mock_title_mode15_response result=1 len=23`, `queue_login_followup_event ... label=title-mode15`), but it still produces no `trace_title_login_write` entries and no change in `trace_title_login_state`: `mode` remains `0`, `listPtr` remains `0`, and `target48/4c/50/54` remain stable across both follow-up and primary-success events.
- evidence: `bin/logs/net_trace.log` ticks `190` and `217`, with `trace_title_login_state` unchanged before/after both `fire_event slot=0/1` deliveries and no additional field-write logs beyond the earlier local `focus/saveDefault/state0` writes.
- conclusion: even the narrower `type=1, subtype=15` family is still not being consumed by the active `010534b4` screen-local state machine on the live path. This makes the remaining blocker more likely to be an earlier local mode/router gate, or a different owner/parser entirely, rather than just the wrong follow-up subtype payload.

## 2026-06-04
- verified: the current `R9+0x94A4` business/event object that owns the queued `event=7` wrapper is not a title-login object at all. Static CBE RE shows `0x01034922` initializes that object with four callbacks (`sub_10348FC`, `sub_1034874`, `sub_103478E`, `sub_1034868`) and stores it to `*(R9+0x94A4)`. The only direct caller of `0x01034922` is `0x01003856 -> scene_system_bootstrap`.
- verified: this means the `event=7` wrapper and its current callback family are already scene/bootstrap-owned before login completes. That matches runtime logs where `sceneObj` is live during the login screen and `net_state_observe cb44=01037473` never changes while `activeScreen=010534b4`.
- evidence: static xref `scene_system_bootstrap -> sub_1034922`, plus decompile of `sub_1034922`, `net_wrapper_event_dispatch`, and the installed callbacks; runtime `runtime_state` logs showing `sceneObj=0500b210` and `cb44=01037473` while the title login screen is still active.
- conclusion: the deeper problem is likely not just “wrong login follow-up subtype”. The emulator/runtime has already installed the scene-side event packet owner on the shared net/business slot, so login replies are being delivered through a scene/bootstrap owner instead of a title-login owner. That explains why repeated login responses never mutate the title-local `R9+0x29E8` state block.

## 2026-06-04
- changed: added two new read-only traces aimed at the shared event-owner takeover question. `trace_scene_owner_site` now fires at `0x01003856 -> scene_system_bootstrap` and `0x01034922 -> shared_event_owner_init`, while `trace_shared_event_owner_write` watches the actual global owner slot write at `Global_R9 + 38056`.
- verified: rebuilt successfully with `make` after wiring the new owner-slot write watcher into `hookRam.c` and adding the new code-site trace points in `src/main.c`.
- purpose: distinguish between two remaining possibilities on the stuck login path: either scene/bootstrap really installs the shared owner first and keeps it for the whole title stage, or some later title-side code should overwrite the same slot but currently never runs. The next rerun should make that visible without changing behavior.

## 2026-06-04
- verified: the new owner-slot traces settle the overwrite question too. On the newest rerun, `trace_scene_owner_site` fires at `tick=0` first for `scene_system_bootstrap`, then for `shared_event_owner_init`, and the only observed `trace_shared_event_owner_write` is the immediate bootstrap install `slot=0105a078 old=00000000 new=010560f0 last=01034960 tick=0`. No later title-stage code writes that slot again.
- verified: by the time the title login screen is added (`01056204 -> 0105A814 -> 010534B4` at ticks `69..78`), the shared owner is already fixed to the scene/bootstrap object, and all later login replies still run with the same wrapper state: `net_state_observe cb14=00000000 cb44=01037473`, `sceneObj=0500b210`, unchanged through both `title-mode15` and primary `1/1/1` response delivery.
- evidence: latest `bin/logs/net_trace.log` lines `4..6`, `10`, `72..89`, `191..193`, and `621..644`.
- conclusion: the current stuck login path is not just “title failed to overwrite a shared slot later”; the scene/bootstrap owner is installed before the title login screen exists and is never replaced during the login interaction. The next useful static question is therefore who was supposed to own `R9+0x94A4` before `scene_system_bootstrap()` ran, or why that bootstrap is executing before the title login stage at all.

## 2026-06-04
- verified: the immediate scene/bootstrap install is now explained by a direct startup branch rather than by any login reply. `sub_1002538()` calls `sub_1003C5A()`, and `sub_1003C5A()` returns `scene_system_bootstrap` unconditionally; `sub_1002538()` then invokes that function pointer directly and sets `R9+21300 = 1`.
- verified: the main startup gate `sub_100254A()` enters that direct branch whenever `sub_100AFCA() == 1`. The alternate helper `sub_100D04A()` also falls back to `sub_1002538()` under several local conditions, so both the early gate and one later update/download path converge on the same direct scene bootstrap call.
- verified: `sub_100AFCA()` itself defaults to `1` unless the callback table at `R9+19848` exists, accepts `(1002,1)`, and then returns a nonzero mode on `(1002,3)`. On the current binary, the other obvious local mode stub `sub_10001FE()` is hardcoded to `0`, reinforcing that the current startup path is being decided by local platform/config gates rather than by network replies.
- evidence: static decompile of `sub_100254A()` (`0x0100254A`), `sub_1002538()` (`0x01002538`), `sub_1003C5A()` (`0x01003C5A`), `sub_100D04A()` (`0x0100D04A`), and `sub_100AFCA()` (`0x0100AFCA`).
- conclusion: the present “login screen but scene-owned event router” state is consistent with a startup mode gate that is already choosing the direct-enter/game bootstrap path locally before the title login interaction finishes. The next narrow target is to identify what the `R9+19848` callback family represents and why it currently leaves `sub_100AFCA()` true on this emulator path.

## 2026-06-04
- verified: the callback family behind `R9+19848` now has a strong repo-side mapping. Static code and emulator config line up with the `DL_PAY` manager table: `src/config.h` defines `VM_DL_PAY_MANAGER_ADDRESS` / `VM_DL_PAY_FUNC_LIST_ADDRESS`, `vmFunc.c` installs that table via `vm_configManagerTableCount(..., 16)`, and `hook_vm_dl_pay_func()` currently handles every `DL_PAY` call by logging `dl_wpay_call idx=%u` and returning `0`.
- verified: the CBE-side callers that use `R9+19848` all come from `vmdlEnterPay.c` logic: `sub_100AFCA()` queries `(1002,1)` and `(1002,3)` through that table, while `sub_1002B24()` uses the same table to dispatch entry/payment events and `sub_100ABAA()` scans pending records for code `1002`.
- evidence: repository grep hits in `src/config.h`, `src/vmFunc.c`, `src/main.c` (`hook_vm_dl_pay_func`), plus static decompile of `sub_1002B24()` / `sub_100ABAA()` with source-string tag `vmdlEnter\\vmdlEnterPay.c`.
- hypothesis: if `R9+19848` is correctly pointing at the emulator's `DL_PAY` manager, then the current all-zero `hook_vm_dl_pay_func()` behavior itself may be what leaves `sub_100AFCA()` true and keeps selecting the direct-enter bootstrap path. If the pointer is null instead, then the same gate is failing even earlier. A new `trace_enter_mode_gate` watcher has been added to separate those two cases on the next rerun.

## 2026-06-04
- changed: added an even narrower gate-result watcher for `sub_100AFCA()`. `trace_enter_mode_gate_result` now fires at `0x0100AFE4` and `0x0100AFF8`, i.e. immediately after the `(1002,1)` and `(1002,3)` callback queries return.
- verified: rebuilt successfully with `make` after wiring the new watcher into `src/main.c`.
- purpose: the last rerun proved `R9+19848` is non-null and points at a valid manager table (`payMgr=0A003400`, `payFn0/1/2=0C003000/4/8`) before `sub_1002538()` is entered, so the remaining ambiguity is no longer “table missing” but “what exact return value is forcing `sub_100AFCA()` to evaluate true”. The new watcher should decide that in one more run.

## 2026-06-04
- corrected: the first `trace_enter_mode_gate_result` hook points were one instruction too early. `0x0100AFE4` / `0x0100AFF8` are the `BLX` and `MOVS` sites inside `sub_100AFCA()`, so the earlier “result=0x3EA” line was just the outgoing argument `R0=1002`, not the callback return value.
- changed: moved the real post-call watcher to `0x0100AFE6` and `0x0100AFF4`, while keeping explicit pre-call traces at `0x0100AFE4` / `0x0100AFF2`; also widened `dl_wpay_call` logging to include `r1`, `lr`, and `tick`.
- verified: rebuilt successfully with `make` after the trace-point fix.

## 2026-06-04
- verified: the corrected `sub_100AFCA()` post-call watcher now settles the startup gate. On the newest rerun, `trace_enter_mode_gate_result label=sub_100AFCA_after_call_1002_1` reports `result=00000000`, with `last=0c003000`, so the first `DL_PAY` callback query (`code=1002, mode=1`) is actually executed through the emulator stub and returns `0`.
- verified: because that first callback result is `0`, `sub_100AFCA()` exits through its default-true path before the second `(1002,3)` query is ever reached. This is why there is no corresponding `sub_100AFCA_after_call_1002_3` line in the same run.
- evidence: latest `bin/logs/net_trace.log` lines `4..7`, especially `before_call_1002_1` -> `after_call_1002_1` with `last=0c003000`, followed immediately by `sub_1002538()` and then `scene_system_bootstrap`.
- conclusion: the current direct-enter behavior is now pinned to emulator-side `DL_PAY` semantics, not to missing tables or later login packets. The first `DL_PAY` gate callback is returning `0`, which makes `sub_100AFCA()` evaluate as true and immediately jump into `scene_system_bootstrap()`.

## 2026-06-04
- changed: applied the narrowest possible `DL_PAY` stub experiment in `hook_vm_dl_pay_func()`. For `idx=0` only, when the client queries `(code=1002, mode=1)` or `(code=1002, mode=3)`, the emulator now returns `1` instead of the previous unconditional `0`; all other `DL_PAY` calls still return `0`.
- purpose: this directly tests the startup gate hypothesis without perturbing unrelated manager semantics. If the next rerun stops calling `sub_1002538()` immediately and reaches `sub_100AFCA_after_call_1002_3`, we will know that `DL_PAY` return semantics were the local cause of the premature scene/bootstrap path.

## 2026-06-04
- corrected: the address attribution above was off by one manager family. `last=0c003000` maps to `VM_MANAGER_BILLING_FUNC_LIST_ADDRESS`, not to `VM_DL_PAY_FUNC_LIST_ADDRESS`; `0x0C003000` is the first `BILLING` entry, while `DL_PAY` starts later at `0x0C006000`.
- verified: the real stub driving `sub_100AFCA()` is therefore `hook_vm_manager_billing_func()` entry `idx=1`, which previously hardcoded `R0 = 0` for every call. That exactly matches the observed `sub_100AFCA_after_call_1002_1 = 0`.
- changed: moved the narrow startup-gate experiment to the real owner. `hook_vm_manager_billing_func()` now returns `1` for the specific billing-gate queries `(code=1002, mode=1)` and `(code=1002, mode=3)`, and logs them as `billing_gate_call`; unrelated billing entries are unchanged.

## 2026-06-04
- verified: once the billing startup gate was opened, the next emulator-side blocker moved to the generic manager table. The first new assert was `VmGetMixMenuLength` at `src/main.c` line `7552`, indicating the client advanced into a previously unreached startup/UI metadata path.
- changed: replaced the hard asserts for manager idx `91..94` (`VmGetMixMenuLength`, `VmGetMixMenudata`, `VmGetWpayCBMInfo`, `VMGetCurVerAllInfo`) with minimal empty-capability stubs that return `0` and emit argument traces to `stdout_trace.log`.
- changed: for the output-style calls (`92..94`), the emulator now also zero-fills up to 32 bytes at the first two guest pointers when present, to keep follow-on reads deterministic without inventing menu/version payload semantics yet.
- status: hypothesis. The current implementation treats the mixed-menu / wpay metadata family as absent rather than reconstructed. The next rerun should show whether this path only probes optional capability menus or requires a richer payload contract.

## 2026-06-04
- verified: the minimal `MixMenu` stubs are accepted by the current startup path. The newest rerun shows only two startup-time probes, both handled cleanly with no new assert:
  - `VmGetMixMenuLength(r0=0c00056c, r1=0c000564, r2=01009e60, r3=0c001400) -> 0`
  - `VmGetMixMenudata(r0=0c000570, r1=0, r2=0, r3=0) -> 0`
- verified: the billing-gate experiment did change `sub_100AFCA()`, but it did **not** eliminate the later direct-enter fallback. Runtime now shows `(1002,1)` and `(1002,3)` both returning `1`, which means `sub_100AFCA()` itself should evaluate false; nevertheless, execution still reaches `sub_100D04A()` and then `sub_1002538() -> scene_system_bootstrap`.
- evidence: latest `bin/logs/net_trace.log` lines `4..29`, plus `stdout_trace.log` `manager_stub api=VmGetMixMenuLength/MixMenudata`.
- conclusion: the remaining root cause is no longer the first billing gate. The still-active direct-enter selection has narrowed to `sub_100D04A()`'s local fallback conditions, specifically `n2 == 1` from `sub_100B95C()` or a zero return from `sub_1000648()`. A new `trace_wpay_update_gate` watcher has been added at `0x0100D052` and `0x0100D064` to distinguish those cases on the next rerun.

## 2026-06-04
- verified: `trace_wpay_update_gate` now resolves the split inside `sub_100D04A()`. On the newest rerun, `sub_100B95C()` returns `1` immediately (`r0=1` at `0x0100D052`), so `sub_100D04A()` jumps straight to `sub_1002538()` without ever calling `sub_1000648()`.
- verified: the same rerun also shows `CDownGetFileNameByAppID appid=9990 missing Wpay9990Ker42WqvgaV100.CBM -> 0`, and repository search confirms no `Wpay9990Ker42WqvgaV100.CBM` exists under the workspace. So the current `n2=1` result is not coming from a successfully loaded local Wpay package; it is coming from the fallback branch inside `sub_100B95C()` after the local file/version lookup path fails.
- evidence: latest `bin/logs/net_trace.log` lines `16..18`, plus workspace file search for `Wpay9990Ker42WqvgaV100.CBM`.
- next: identify the owner of `R9+19824` callbacks `+356` / `+360` used by `sub_1001576()` / `sub_10015DC()`, because those platform callbacks now appear to be the remaining levers deciding whether missing local Wpay data still maps to `n2=1` (direct-enter) or to one of the update-flow states.

## 2026-06-04
- verified: the `R9+19824` callback addresses used by `sub_1001576()` / `sub_10015DC()` resolve to SYS-manager idx `89/90`, i.e. the current emulator stubs for `vmIsInnerApp` and `vmGetInnerAppVer`.
- changed: narrowed `vmIsInnerApp` so that the specific probe coming from `sub_1001576()` (`last=0x01001582`) now reports `0` when `Wpay9990Ker42WqvgaV100.CBM` is missing locally, instead of always forcing `1`.
- purpose: treat the absent Wpay package as “not an installed inner app” rather than as a successful built-in package, so the startup flow can keep progressing without a local `Wpay9990...CBM` sample. A new `sys_inner_app_probe` trace records this decision.

## 2026-06-04
- corrected: the first narrow `vmIsInnerApp` probe hook used the wrong caller address. Runtime shows the actual `sub_1001576()` callback reaches the SYS stub with `last=0x0100158C`, not `0x01001582`, so the previous missing-file override never triggered (`missingWpay=0 result=1` even while `CDownGetFileNameByAppID` logged the file as absent).
- changed: updated the `vmIsInnerApp` caller filter to `0x0100158C` so the missing-`Wpay9990Ker42WqvgaV100.CBM` override now applies to the real probe site.

## 2026-06-04
- corrected: forcing `vmIsInnerApp=0` for the missing-`Wpay9990...CBM` case is not a viable contract on this binary. The next rerun immediately hit `vMAssert file=vmdlEnter\\vmdlEnterDown.c line=720`, called through `sub_1000636()` (`lastPc=0x01000644`), proving the startup/down path treats that branch as invalid.
- changed: reverted the narrow `vmIsInnerApp=0` override and instead virtualized the Wpay metadata callback `CDownGetFileNameByAppID` (billing idx `19`). When the local `Wpay9990Ker42WqvgaV100.CBM` file is absent, the emulator now still reports the app name/type/version tuple (`Wpay9990Ker42Wqvga`, type `0`, version `100`) and returns success, logged as `virtualMissingWpay`.
- purpose: preserve the binary's expectation that the inner Wpay app exists, while still allowing a missing local file sample to map to an update-needed state (`n2=2`) instead of the invalid assert path (`n2=0`) or the old direct-enter path (`n2=1`).

## 2026-06-04
- changed: per current debugging focus, restored `vmIsInnerApp` (SYS idx `89`) to its earlier stable behavior of always returning `1`; this backs out the temporary per-caller experiment and avoids re-entering the `vmdlEnterDown.c:720` assert path while login-result work continues.
- changed: `builtin-login` now branches on the submitted account string parsed from request field `userName` / `username`. If the account is exactly `1234`, the emulator returns a one-object failure reply (`top=1,1,<subtype>`, `result=2`, `information=\"password error\"`); otherwise it returns the existing success payload for that subtype.
- purpose: exercise `net_handle_login_response() -> login_response_result_dispatch()` more directly on the primary login chain, with a controllable failure case that should land in the parser's confirmed `result == '2'` message/error branch.

## 2026-06-04
- verified: with `vmIsInnerApp` restored and `CDownGetFileNameByAppID` virtualized, the startup gate now advances further: `sub_100B95C()` returns `2`, `sub_1000648()` returns `1`, and the binary enters the WPay/update path instead of the old direct-enter path.
- changed: replaced the next billing assert point `CDownIsWPay` (billing idx `38`) with a minimal stub that logs its arguments and returns `1`.
- purpose: keep the newly reached WPay/update path alive long enough to get back to the login/result-dispatch work, instead of breaking on the first unimplemented capability probe.

## 2026-06-04
- changed: backed out the startup/WPay gate experiments so the emulator can return to the earlier stable title/login startup path while login-result work continues. `hook_vm_manager_billing_func()` idx `1` no longer forces `(1002,1)` / `(1002,3)` success, `hook_vm_dl_pay_func()` no longer overrides the same probe family to return `1`, `CDownGetFileNameByAppID` no longer virtualizes a missing `Wpay9990Ker42WqvgaV100.CBM` into a fake installed app, and `CDownIsWPay` is back to its original unimplemented/asserting state.
- preserved: the login mock split remains active. `builtin-login` still parses request field `userName` / `username` and now returns `result=2` with `information="password error"` for account `1234`, while other accounts keep the existing success payload.
- purpose: stop the black-screen/WPay-update regression introduced by the startup experiments, and isolate the current debugging target back to `net_handle_login_response()` and its primary/alternate result-dispatch behavior.

## 2026-06-04
- verified: the new account-gated failure reply for `1234` is emitted exactly as intended on the wire. Latest `bin/logs/net_packets.log` shows repeated `1/1/1` login requests with `userName=1234`, and the emulator responds each time with a one-object WT packet `1/1/1` carrying `result=2` plus `information="password error"`.
- verified: despite the correct mock-side emission, the active title screen still shows no visible error reaction and no local state transition. `bin/logs/net_trace.log` shows `mock_login_account_gate account=1234 requestSubtype=1 result=2`, followed by normal `fire_event slot=0/1 event=7`, but `trace_title_login_state` remains unchanged across `pre_data_event_05017509` / `post_data_event_05017509` (`mode=0`, `listPtr=0`, `roleFamily=0`, unchanged targets), and there is still no `trace_title_screen_callback`.
- conclusion: the current `1/1/1 {result=2, information}` packet is reaching the transport layer, but it is still not entering the title-local login result owner that should display the fixed failure prompt for result `'2'`. The blocker is therefore upstream of the actual prompt text branch: current live event routing/owner selection is still wrong or incomplete for login responses.

## 2026-06-04
- verified: the delayed-login-success experiment now proves the local mode-1 timeout branch is real on the active `010534b4` title screen. In the latest rerun, the delayed main login-success `event=7` was still pending (`slot0[e=7 d=59]` at `tick=541`, still `d=34` at `tick=566`), so the following title-local behavior happened before any accepted `1/1/x` success object was delivered.
- verified: `obj8_stateA` really crosses the built-in `>300` threshold. At `tick=541`, `trace_title_flow_action label=sub_5B0_check_byte2` reports `obj8_stateA=301`, immediately followed by `trace_title_router_gate label=sub_5B0_queue_activate_role_manage`. This confirms the already recovered `sub_5B0` branch is not hypothetical: the current local flow does queue the role-manage activator once the counter exceeds `300`.
- verified: the same threshold hit also performs a local reset/disarm of the child widgets, but not of the owner mode. In the same `tick=541` window, `obj8.stateA` is reset `301 -> 0`, `obj8.flag10` clears `1 -> 0`, `objC.flag10` / `titleLoadingGate` clears `1 -> 0`, while `ownerObj.byte2` remains `0` and `obj28=1 obj2c=1 local10340=1 local10344=1` stay unchanged on subsequent `tick=542..566` samples.
- verified: no visible screen handoff or later `byte2=3` write occurs before the run ends. The log contains `sub_5B0_queue_activate_role_manage`, but there is still no `screen_manager idx=6 remove r0=010534b4`, no `screen_manager idx=4 add r0=01053450`, and no `ownerObj.byte2 ... new=00000003` in the same delayed-success session.
- evidence: `bin/logs/net_trace.log` around `tick=541..566`, especially the lines with `obj8_stateA=301`, `sub_5B0_queue_activate_role_manage`, the child-state resets at `last=050172de/e0/e6`, and the still-pending delayed slot counters.
- next: stop debating whether the `>300` branch exists; it is now runtime-confirmed. The next narrow target is the callback path scheduled by `sub_10C6(&unk_82C, title_activate_role_manage_screen)`: trace whether that queued role-manage activator is ever executed, delayed behind another local queue, or canceled before it can switch screens.

## 2026-06-04
- changed: pushed the next read-only layer one hop deeper into the mode-1 callback indirection. `src/main.c` now also watches:
  - `ownerObj.modeObjPtr` (`ownerObj+0x90`, i.e. the pointer slot passed as `a1` into `01048F8C/01048FB4`)
  - the pointed object's `+4/+8/+0xC` methods in `trace_title_mode_callback` as `modeObjCb4/modeObjCb8/modeObjCbC`
- purpose: distinguish whether the repeatedly exercised `sub_840_mode1_call_cbA` is landing on a benign helper object, a popup object, or the actual role-transition object that should eventually clear `obj2C/local10344`.
- validation: rebuilt successfully with `make`; still read-only instrumentation only.

## 2026-06-04
- verified: the new mode-callback watcher resolves the next split on the active `010534b4` title path. The two mode-1 callback slots are populated once during early screen setup, before mode-1 is armed:
  - `ownerObj.mode1CbA = 01048F8D`
  - `ownerObj.mode1CbB = 01048FB3`
  Both writes happen at `tick=68`, long before `sub_938(event=5)` later sets `obj2C/local10344 = 1`.
- verified: once `obj2C == 1` and `obj8.flag10` drops to `0`, runtime does not sit idle waiting for a missing callback target. `sub_840_mode1_call_cbA` actually fires repeatedly on the current path (`tick=114`, `116`, `121`, `146`, `172`, ...), while `sub_840_mode1_call_cbB` never appears in the same run. There is also still no `sub_840_mode2_call_sub_74A` or `sub_840_mode2_call_role_manage`, because the owner never transitions from `obj2C == 1` to `obj2C == 2`.
- verified: those callbacks do not disarm the title owner. Across the same window, `obj28=1 obj2C=1 local10340=1 local10344=1` stay unchanged, and the path remains stuck in the `sub_5B0_mode1_branch` loop until accepted login success later sets `byte2=3`.
- static correlation: `01048F8C` (`mode1CbA`) is a small main-CBE adapter that checks an object pointer plus `sub_1015F40(n2)` and then calls the pointed object's `+4` method when allowed; `01048FB2/01048FB4` (`mode1CbB`) is a sibling adapter that would call the same object's `+8` method with two arguments. So the current blocker is not "uninitialized mode-1 callback slots"; it is that the currently exercised mode-1 callback family does not lead into any `sub_74A()` / `sub_722()`-style reset on this path.
- evidence: `bin/logs/net_trace.log` lines with `ownerObj.mode1CbA/mode1CbB` at `tick=68`, repeated `trace_title_mode_callback label=sub_840_mode1_call_cbA` at `tick=114/116/121/146/172`, the absence of `sub_840_mode1_call_cbB` / `sub_840_mode2_call_*`, and paired static recovery of `0x01048F8C`, `0x01048FB2`, and `0x01048FB4` in `江湖OL.CBE`.
- next: the most useful next target is now upstream of these adapters: identify the concrete object stored behind the mode-1 callback indirection and the real `+4` / `+8` methods it dispatches into. That should answer whether current mode-1 input is reaching a benign popup/helper object instead of the title-role transition object that would eventually clear `obj2C/local10344`.

## 2026-06-04
- changed: added one narrower read-only watcher layer around the mode-1 local callback slots and `sub_840()`'s indirect call sites. `src/main.c` now:
  - watches owner-local writes to `ownerObj+0x98/+0x9C` as `ownerObj.mode1CbA/mode1CbB` (these are the two `R9+10452/+10456` callback slots used when `obj2C == 1`)
  - traces runtime `sub_840()` call sites at relocated `0x0501748E/0x05017496` (`sub_840_mode1_call_cbA/cbB`) and `0x0501745E/0x050174AA` (`sub_840_mode2_call_sub_74A` / `sub_840_mode2_call_role_manage`)
- purpose: settle the next narrow split without changing behavior: whether the current armed mode-1 path ever receives concrete callback targets in `R9+10452/+10456`, and whether runtime ever actually reaches the local input-side exits that can lead into `sub_74A()` / `sub_722()`.
- validation: rebuilt successfully with `make`; no behavior changes intended beyond extra trace output.

## 2026-06-04
- corrected: the latest `sub_59E/sub_5B0` static pass disproves the previous shorthand that "the stale `&unk_C20` queue makes `sub_59E` stop calling `sub_564()`". `sub_59E()` has only one local gate (`R9+10303` / `popupSkip`); otherwise it always tail-calls `sub_5B0()`.
- verified: the real switch sits inside `sub_5B0()`. Static recovery shows there are only two `BL sub_564` callsites in the whole title module (`0x5D2` and `0x68C`), both inside `sub_5B0()`:
  - `0x5D2`: the `obj4.state8 > 0` branch (`*(_WORD *)(obj4+8) > 0`)
  - `0x68C`: the idle branch with `obj4.state8 <= 0` and `obj2C == 0`
- verified: the current long delayed-success loop is not taking either of those branches. Runtime at `tick~781+` shows repeated `sub_59E_popup_dispatch -> sub_5B0_load_mode -> sub_5B0_mode1_branch -> shared_popup_bridge_cb30_guard -> sub_5B0_check_byte2`, with stable `obj28=1 obj2c=1 local10340=1 local10344=1 byte2=0`. That matches the static `n2 == 1` branch at `0x6CA`, which never calls `sub_564()` at all.
- verified: the local input dispatcher `sub_840()` is now statically matched to the same mode byte. When `obj4.state8 <= 0`:
  - `local10344 == 1` (`obj2C == 1`) waits for `obj8.flag10 == 0`, then routes mode-1 local input through callback slots `R9+10452` / `R9+10456`
  - `local10344 == 2` (`obj2C == 2`) uses a different local path: `n3 == 3` calls `sub_74A()`, and `n3 == 0 && *a3 == 0x2000` calls `title_activate_role_manage_screen()`
- verified: `sub_74A()` contains the only currently confirmed mode-local reset transition out of this family. It hit-tests two local regions:
  - `(10, 359)` -> `sub_722()` full reset/disarm
  - `(181, 359)` -> `title_activate_role_manage_screen() -> sub_EC(9)`
- implication: the missing `event=5 -> cb24/open_channel -> alloc_outgoing_game_event()` restart is not directly caused by the stale queue item. The earlier `sub_938(event=5)` mode-1 arm (`obj2C/local10344 = 1`) is what diverts `sub_5B0()` away from both `sub_564()` callsites; the stale queue is a parallel blocker for the later role-manage callback, not the reason `sub_564` disappears.
- verified: the only currently confirmed full disarm helper is `sub_722()`. Static decompile shows it clears `R9+10336/+10340/+10332/+10344` (owner `+0x24/+0x28/+0x20/+0x2C`), resets the child counter, and re-enables `objC.flag10`. Its current callers are `sub_74A()`, `role_list_screen_handle_action()`, and `role_manage_screen_handle_role_list_nav()`, i.e. role-list / role-manage local paths rather than the current `99/1` branch.
- evidence: static decompile/disassembly of `sub_59E()` / `sub_5B0()` / `sub_722()`, `xrefs_to sub_564` and `xrefs_to sub_722` in `mmTitleMstarWqvga.cbm`, plus `bin/logs/net_trace.log` `tick~781..788` showing the persistent `sub_5B0_mode1_branch` loop with `obj2c=1`.
- next: shift the narrow question again. The active contract gap is now "what real title/local event is supposed to clear `obj2C/local10344` or invoke the `sub_722()`-style reset after `sub_938(event=5)` arms mode-1", not "why stale queue suppresses `sub_564()`".

## 2026-06-04
- changed: added the next read-only watcher set for the confirmed `>300` branch. `src/main.c` now logs `trace_title_callback_queue` at runtime `sub_1032` entry/commit (`0x05017C02`, `0x05017C86`) and at `title_activate_role_manage_screen()` entry (`0x0501711A`).
- purpose: separate three still-ambiguous outcomes on the delayed-success title path without changing behavior: queue never armed, queue armed but never consumed, or queue consumed and then blocked by a later local screen/state gate.

## 2026-06-04
- verified: the new callback-queue watcher resolves the remaining ambiguity on the delayed-success title path. When the first `99/1` exchange happens, `sub_1032_queue_enter` and `sub_1032_queue_commit` both fire at `tick=110`, committing a queue entry with `queueText=050177f0` and `queueCallback=00000000`.
- verified: when the later local timeout branch hits `obj8_stateA=301`, `sub_5B0_queue_activate_role_manage` does call into `sub_1032`, but only `sub_1032_queue_enter` fires (`tick=486`) and there is no matching `sub_1032_queue_commit`. The entry-side snapshot already shows `queueActive=1`, `queueText=050177f0`, and `queueCallback=00000000`, so the queue is still occupied by the older no-callback item before the role-manage request arrives.
- verified: because the second enqueue is rejected at the existing-active check, `title_activate_role_manage_screen()` itself never executes in the same run. No such trace appears before the delayed login-success packet finally lands, sets `ownerObj.byte2=3`, and the usual `010534b4 -> 01053450` teardown resumes.
- evidence: `bin/logs/net_trace.log` lines for `sub_1032_queue_enter/sub_1032_queue_commit` at `tick=110`, `sub_5B0_queue_activate_role_manage` + `sub_1032_queue_enter` at `tick=486`, the absence of a second `sub_1032_queue_commit` or `title_activate_role_manage_screen`, and the later `ownerObj.byte2 ... new=00000003` / `screen_manager idx=6 remove r0=010534b4`.
- next: the blocker is no longer “timeout branch missing” or “queued callback not consumed”. The next narrow target is why the earlier queue entry (`queueText=050177f0`, `queueCallback=0`) remains active for hundreds of ticks without a clear/reset, and what local callback or state transition is supposed to release that queue before the role-manage timeout path tries to use it.

## 2026-06-04
- changed: widened the read-only queue tracing around the stale title-local popup item. `src/main.c` now logs direct writes to `queueStyle/queueFlag1/queueActive/queueText/queueCallback`, and also traces the most likely consumer/reset functions: `sub_F50`, `title_activate_popup_screen_if_idle`, `sub_FF0`, `role_manage_screen_enter_action_menu`, `role_manage_screen_handle_network`, `sub_5922`, and `sub_5A74`.
- purpose: distinguish whether the long-lived `queueText=050177f0` item is never revisited, revisited but blocked by input-gate logic, or explicitly cleared by a later local release function before the role-manage timeout path tries to enqueue its own callback.

## 2026-06-04
- verified: the stale queue item is now pinned to the `99/1` generic-cleanup branch in `sub_938(event=7)`. Runtime `queueText=050177f0` maps back to relocated static data `&unk_C20`, and static `sub_938` shows that exact data object is enqueued only on the early `if (packet_check(..., 10, 19)) sub_10C6(&unk_C20, 0)` branch before the normal `type=1/subtype=2|3|6` scan.
- verified: on the newest rerun, the queue item is created once and then never touched again before login success lands. After `tick=110` there are no further `trace_title_queue_write` lines for `queueText/queueCallback/queueActive`, and none of the suspected consumer/reset entry traces (`title_activate_popup_screen_if_idle`, `sub_F50`, `sub_FF0`, `sub_5922`, `sub_5A74`, `role_manage_screen_enter_action_menu`, `role_manage_screen_handle_network`) appear at all in the same delayed-success session.
- implication: the current blocker is no longer “clear function runs but gate fails”. The old `&unk_C20` popup/queue item simply is not being revisited on the active emulator path before the later `>300` role-manage timeout wants the same queue.
- evidence: `bin/logs/net_trace.log` `tick=110` queue writes (`queueText=050177f0`, `queueActive=1`), the total absence of later queue writes or consumer traces, plus static `sub_938` branch at `0xA2E..0xA32`.
- next: the next narrow target is upstream of the queue clearers. Either the emulator is missing the local callback/timer that normally re-enters `title_activate_popup_screen_if_idle()` after `sub_938` enqueues `&unk_C20`, or the `packet_check(...,10,19)` path itself is only supposed to happen under a different pre-login contract and should not be taken on this screen at all.

## 2026-06-04
- changed: added one more read-only watcher layer above the stale `&unk_C20` queue item. `src/main.c` now traces the post-call result site of `sub_938`'s `packet_check(...,10,19)` branch and the four local popup-driver functions that govern the `base+10300/10301/10303` counters (`sub_4A4`, `sub_454`, `sub_564`, `sub_59E`).
- purpose: distinguish whether the current mismatch is caused by the wrong early packet-check branch being taken at all, or by the popup-driver/timer state machine simply never advancing far enough to revisit the queued `&unk_C20` item after it is created.

## 2026-06-04
- verified: the popup/timer layer is not missing on the delayed-success title path. After the first `99/1` exchange commits `queueText=050177f0` / `queueActive=1` at `tick=110`, `sub_59E_popup_dispatch` keeps re-entering on the active `010534b4` screen with the same live queue snapshot (`queueActive=1`, `queueText=050177f0`, `queueCallback=0`, `popupTarget=0`, `popupCount=0`).
- verified: the later accepted login-success delivery is not recreating that stale queue item. At the delayed success window (`tick=536`), `trace_title_router_gate label=sub_938_packetcheck_10_19_result` reports `r0=0`, matching the static `CMP R0,#0 ; BNE loc_A2E` gate and proving `packet_check(...,10,19) -> sub_10C6(&unk_C20, 0)` did not fire on that accepted-object path.
- implication: the blocker has moved one layer deeper than the popup timer. The queue is being revisited by the local driver, but it never advances into a visible popup consumer/reset path before accepted login success sets `ownerObj.byte2=3` and `sub_5B0` tears down `010534b4`.
- evidence: `bin/logs/net_trace.log` around `tick=110` (first queue commit), the repeated `sub_59E_popup_dispatch` samples through roughly `tick=478..536`, the late `sub_938_packetcheck_10_19_result r0=0`, and the immediate `ownerObj.byte2 ... new=00000003` / `screen_manager idx=6 remove r0=010534b4` sequence at `tick=536`.
- next: trace the popup object's own consumer/dispatch layer beneath `sub_59E/sub_564`, rather than the high-level timer. The remaining gap is now most likely in the local object method(s) that should convert `queueActive=1` + `queueText=&unk_C20` into a real popup activation or queue clear.

## 2026-06-04
- changed: pushed the next read-only trace layer beneath the title popup timer into the shared objC bridge that `sub_564()` actually polls. `src/main.c` now logs `trace_title_popup_bridge` at CBE runtime `0x01034874/0x010348FC/0x0103478E/0x01034868/0x01034922`, corresponding to objC `+0x28/+0x24/+0x2C/+0x30` and bridge init, and it also extends `trace_title_child_state_write` to watch objC fields `+4/+8/+0xC/+0x10/+0x11`.
- purpose: settle the now-narrowest split without changing behavior: whether the repeated `sub_59E/sub_564` revisits are actually entering the shared popup bridge and mutating its internal state, or whether the queue stalls one layer earlier before those objC methods can consume `&unk_C20`.

## 2026-06-04
- verified: the shared popup bridge is active on the current `010534b4` path, but it degrades into a guard-only loop once the stale queue item is committed. Before the `99/1`-era queue becomes active, the bridge still runs a richer sequence: `shared_popup_bridge_cb28_poll`, then a state write `objC.stateC -> 3`, then `shared_popup_bridge_cb24_dispatch`, and later `shared_popup_bridge_cb30_guard -> shared_popup_bridge_cb2C_pump` while the child event object still reports `eventField10=1`.
- verified: immediately after the first `99/1` queue commit (`queueText=050177f0`, `queueActive=1`), that richer sequence disappears. On the active title screen, the repeated revisit path becomes only `shared_popup_bridge_cb30_guard`, with stable bridge fields `field4=2`, `stateC=0`, `queueActive=1`, and most importantly `eventObj+16` (`eventField10`) now `0`. Because static `sub_1034868()` only falls through to `sub_103478E()` when `*(_DWORD *)(eventObj + 16)` is non-zero, the bridge now has no reason to enter the pump path anymore.
- implication: the missing step is no longer just "popup queue not consumed". More narrowly, the bridge's child event object is no longer armed for pumping once the stale queue item is live. The next likely gate is therefore the child event object behind `objC+32`, especially its `+16` flag/state field rather than the top-level queue bits themselves.
- evidence: `bin/logs/net_trace.log` around `tick=102` (`shared_popup_bridge_cb28_poll`, `objC.stateC -> 3`, `shared_popup_bridge_cb24_dispatch`), `tick=109` (`shared_popup_bridge_cb30_guard` followed by `shared_popup_bridge_cb2C_pump` while `eventField10=1`), and `tick=110..560` where only `shared_popup_bridge_cb30_guard` remains while `queueActive=1`, `queueText=050177f0`, and `eventField10=0`.
- changed: extended the read-only child-state watcher one more level down so the next rerun can watch `objC.event.byte5`, `objC.event.field10`, and `objC.event.field20` writes directly off the bridge's child event object.
- next: rerun once on the rebuilt binary and inspect who clears or rearms `objC.event.field10`. That should settle whether the stale popup item fails because the child event object is prematurely reset, or because nothing ever rearms it after the queue item is committed.

## 2026-06-04
- verified: the child-event rearm question is now resolved. On the active `010534b4` path, `objC.event.byte5` and `objC.event.field10` are first armed by `event_packet_add_field()` (`last=01034606` and `last=0103460e`), which matches the static helper writing `eventObj+5 = 1` and incrementing `eventObj+16`.
- verified: that same arm is torn back down by the bridge pump path itself before the `99/1` queue item is committed. During the same `tick=107` window, `shared_popup_bridge_cb2C_pump` runs, then `sub_103478E()` clears `objC.event.byte5` (`last=010347aa`), and `event_packet_init()` clears `objC.event.field10` (`last=01034702`) while leaving `objC.event.field20` unchanged (`last=010346f2` only re-stores the existing callback pointer).
- implication: the stale popup item is not failing because some later unrelated code clears `eventObj+16`; it is failing because the bridge's own `cb2C_pump -> event_packet_init()` cycle resets the child event object to an unarmed state and nothing rearms `eventObj+16` after `queueText=050177f0` / `queueActive=1` goes live.
- evidence: `bin/logs/net_trace.log` `tick=107` lines for `objC.event.byte5 -> 1`, `objC.event.field10 -> 1`, `shared_popup_bridge_cb2C_pump`, then `objC.event.byte5 -> 0` and `objC.event.field10 -> 0`; paired static recovery of `event_packet_add_field()` (`0x010345D4`), `event_packet_init()` (`0x010346E0`), and `sub_103478E()` (`0x0103478E`).
- next: stop looking for a mysterious later clearer of `eventObj+16`. The next narrow target is now upstream of `event_packet_init()`: what real contract is supposed to call `event_packet_add_field()` again, or otherwise rearm the child event object after the stale `&unk_C20` queue item has been committed.

## 2026-06-04
- verified: the new mode-object watcher resolves the first layer beneath `sub_840_mode1_call_cbA`. Runtime `modeObjCb4/8/C` values on the active `010534b4` path map cleanly back to two title-module object families:
  - `050192f5/05019399/050193fb` -> relocated `0x2724/0x27C8/0x282A` -> `title_four_choice_screen_handle_action`, `title_render_four_choice_menu_hitboxes`, `title_four_choice_screen_render`
  - `050196ef/050197b1/05019867` -> relocated `0x2B1E/0x2BE0/0x2C96` -> `login_form_handle_action`, `login_form_handle_touch`, `login_form_render`
- verified: the live owner-side object pointer actually switches between those two families. At `tick=78`, `ownerObj.modeObjPtr` is written to `0501f820` during screen setup while `ownerObj.mode1CbA/mode1CbB` become `01048F8D/01048FB3`; later, at `tick=123`, `ownerObj.modeObjPtr` changes from `0501f820` to `05010db0` while the armed mode-1 loop is already active.
- verified: after `obj8.flag10` drops to `0`, the repeated `sub_840_mode1_call_cbA` path is therefore not reaching a hidden role-transition object. It first dispatches into the four-choice UI object, then into the login-form UI object, while `obj2C=1` / `local10344=1` remain unchanged and `sub_840_mode1_call_cbB` / all mode-2 exits stay absent.
- implication: the current mode-1 callback loop is now strongly identified as ordinary title/login UI dispatch, not the missing disarm path that would clear `obj2C/local10344`. The next narrow gap is one level higher again: who calls the tiny pointer-wrapper setter `sub_1048FE0()` to replace `ownerObj.modeObjPtr`, and what real object family is supposed to be installed before the branch can reach `sub_74A()` / `sub_722()`.
- evidence: `bin/logs/net_trace.log` lines for `ownerObj.modeObjPtr` writes at `tick=78` and `tick=123`, repeated `trace_title_mode_callback label=sub_840_mode1_call_cbA`, plus static recovery in `mmTitleMstarWqvga.cbm` of `0x2724/0x27C8/0x282A/0x2B1E/0x2BE0/0x2C96`.

## 2026-06-04
- changed: added the next read-only watcher right at the main-CBE mode-object wrapper. `src/main.c` now traces `sub_1048FE0()` and `sub_1048FEE()` as `trace_title_mode_object_set`, logging `LR`, the slot pointer, old/new object pointer, and the new object's `+4/+8/+C` methods whenever the title owner swaps its mode-local dispatch object.
- purpose: settle the new narrow split without changing behavior: whether the four-choice -> login-form object switch is driven by the expected title flow, or whether some missing branch should have installed a different role/popup object family before the armed mode-1 loop begins.

## 2026-06-04
- verified: the new mode-object-set watcher closes that split. The first `modeObjPtr` installation happens inside `sub_A50()` screen init: runtime `mode_obj_set_ptr lr=0501774b` maps back to relocated static `0x0B7B` inside `sub_A50`, which is the same init path already known to load `UI2/UI7/UI8` and clear `obj28/obj2C`.
- verified: the later `modeObjPtr` swap from `0501f820` to `05010db0` is not an external main-CBE override. Runtime `mode_obj_set_ptr lr=0501938b` maps back to relocated static `0x27BB` inside `title_four_choice_screen_handle_action()`, i.e. the four-choice menu object itself is what replaces the dispatch object.
- static correlation now explains that exact branch. `title_four_choice_screen_handle_action()` uses the wrapper at `v1 + 10464` to install one of two objects:
  - selection `1` -> `(*(...))(v1 + 10444, *(_DWORD *)(v1 + 10792), 0)` at static `0x27B8`, matching the runtime swap to the login-form object family
  - selection `2` -> sibling install from `v1 + 10796` at static `0x27C4`
  - selection `0` with confirm sends `net_build_login_request(1, 12, 5)` directly, and selection `3` exits via `sub_EC(9)`
- implication: the observed `four-choice -> login form` `modeObjPtr` change is the expected local title flow, not a misroute introduced later by the emulator. The still-missing contract is therefore after that swap: what real event/path should replace the login-form-mode object or clear `obj2C/local10344` before accepted login success later sets `byte2=3`.
- evidence: `bin/logs/net_trace.log` lines for `trace_title_mode_object_set` at `tick=78` and `tick=128`, plus static recovery of `sub_A50()` and `title_four_choice_screen_handle_action()` in `mmTitleMstarWqvga.cbm`.

## 2026-06-04
- changed: extended `trace_title_login_write` to watch the four-choice sibling object slots at `base+10792/10796` as `choiceObj1` / `choiceObj2`.
- purpose: make the next rerun answer the most useful remaining object question directly: which concrete pointers are stored in the selection-1 and selection-2 object slots before `title_four_choice_screen_handle_action()` installs one of them into `ownerObj.modeObjPtr`, and whether the non-login sibling points at a materially different role/popup object family.

## 2026-06-04
- verified: the first rerun with write-only `choiceObj1/choiceObj2` watching did **not** log any writes to `base+10792/10796` during the visible title/login interaction window. The existing `modeObjPtr` swap pattern remains unchanged: `sub_A50()` installs the four-choice object at `tick=78`, then `title_four_choice_screen_handle_action()` swaps to the login-form object at `tick=122`.
- implication: those two sibling object slots are probably pre-seeded earlier than the current visible interaction window, or filled through a path that the current write-only watcher does not catch at the moment of interest. So absence of write logs should not be read as "the slots are unused".
- changed: promoted the same two fields into the live mode callback traces. `trace_title_mode_callback` and `trace_title_mode_object_set` now dump `choiceObj1` / `choiceObj2` on every relevant callback/swap, so the next rerun can read their current values even if no fresh write occurs.

## 2026-06-04
- verified: the live-value rerun now exposes both sibling object slots directly. On the active `010534b4` screen, `choiceObj1` is `05010db0` and `choiceObj2` is `0501f808` from the earliest visible mode-object wrapper traces onward.
- verified: `choiceObj1` is the same object later installed into `ownerObj.modeObjPtr` by the `title_four_choice_screen_handle_action()` selection-1 branch. At the swap site, runtime shows `choiceObj1=05010db0` and `newPtr=05010db0` in the same `mode_obj_set_ptr` record.
- implication: selection `1` is now runtime-confirmed as the login-form object slot, while selection `2` remains a distinct pre-seeded sibling object (`0501f808`) that has not yet been exercised on the current path.
- changed: widened the live traces one more step so the next rerun also prints `choiceObj1/choiceObj2` callback triples (`+4/+8/+C`). That should let us classify the selection-2 sibling object without needing it to become active first.

## 2026-06-04
- verified: the callback-triple rerun classifies the selection-2 sibling too. `choiceObj2=0501f808` carries callbacks `0501c0a3/0501c127/0501c181`, which relocate to static `0x54D2/0x5556/0x55B0` in `mmTitleMstarWqvga.cbm`. At that stage we provisionally labeled them as a second login-screen family, pending direct runtime activation.
- verified: `choiceObj1=05010db0` remains the newer login-form object family (`login_form_handle_action/touch/render`).
- hypothesis: before a forced activation run, `choiceObj2` still looked like "another login-family sibling" because it shared the same four-choice installation path and had not yet been observed on screen.

## 2026-06-04
- verified: the newest rerun still does not activate `choiceObj2`. Runtime keeps the initial four-choice object `0501f820` until `tick=290`, then `mode_obj_set_ptr` installs `choiceObj1=05010db0`; there is no `newPtr=0501f808` anywhere in the run.
- verified: the active local path after that swap is still the same armed mode-1 loop. Repeated `sub_840_mode1_call_cbA` samples show `modeObjPtr=05010db0`, `choiceObj2=0501f808`, and stable `obj2C=1` / `local10344=1`, so the old/simple login-screen sibling exists but is not selected on this path.
- evidence: `bin/logs/net_trace.log` `tick=70` setup, `tick=290` `mode_obj_set_ptr`, and later repeated `sub_840_mode1_call_cbA` lines with `modeObjPtr=05010db0`.

## 2026-06-05
- changed: widened the title-login state/write watchers to include the four-choice current selection byte at `base+10733` as `choiceSel`.
- purpose: the current path is now confirmed to prefer `choiceObj1/login_form`, but the selection source is still opaque. The next rerun should answer whether `choiceSel` is initialized to `1`, later rewritten by local input, or preserved untouched across the `99/1` arm and the later `modeObjPtr` swap.

## 2026-06-05
- verified: `choiceSel` is not preinitialized to `1`. The new rerun shows it starts at `0` during `sub_A50()` setup and remains `0` across the early `99/1` data-event window (`pre/post_data_event_05017509` at `tick=108`).
- verified: the decisive `0 -> 1` transition happens later at runtime `last=05019098`, i.e. relocated static `0x24C8` inside `sub_248E()`. Static recovery of `sub_248E()` shows that exact instruction is the generic hit-test helper's `*a9 = n2_2` write, reached when the current pointer/touch falls inside option index `n2_2` and the existing selection byte differs from that index.
- verified: the following `modeObjPtr` swap to `choiceObj1=05010db0` then happens only on the next same-option activation. In `sub_248E()`, when the hit option already equals the current selection, the helper calls the supplied callback with `0x4000`; in this title path that callback is `title_four_choice_screen_handle_action()`, which then installs `choiceObj1`.
- implication: the current `four-choice -> login_form` path is not driven by a timer or hidden network state. It is a two-step local UI sequence:
  1. `sub_248E()` sets `choiceSel = 1`
  2. a later same-hit invocation calls `title_four_choice_screen_handle_action(0x4000)`, which installs `choiceObj1`
- evidence: `bin/logs/net_trace.log` `choiceSel` write at `tick=140`, later `mode_obj_set_ptr` at `tick=156`, and static decompile/disassembly of `sub_248E()` at `0x24C8/0x24CC/0x24D0`.

## 2026-06-05
- changed: built a focused differential experiment into `src/main.c`. When `sub_1048FE0()` is called from `title_four_choice_screen_handle_action()` (`LR=0501938B`) and is about to install `choiceObj1`, the emulator now overrides that one install to `choiceObj2` and also rewrites `choiceSel` to `2`.
- purpose: compare the older `login_screen_*` family against the newer `login_form_*` family without depending on manual option-2 input. The goal is to settle whether both login UIs still converge into the same armed `obj2C/local10344` mode-1 problem.
- validation: rebuilt successfully with `make`; this is an explicit experiment build and should be interpreted separately from natural-path traces.

## 2026-06-05
- verified: the forced-selection experiment works as intended. Runtime now logs `force_title_choice2_install ... forcedPtr=0501f808 choiceSelNew=2`, immediately followed by `mode_obj_set_ptr ... newPtr=0501f808`, so the active mode object is definitely switched away from `choiceObj1` and into `choiceObj2`.
- verified: direct on-screen observation disproves the earlier "old/simple login-screen" label. The forced `choiceObj2` page renders as a recharge selection screen titled `江湖充值`, with options `快速充值` / `手动充值`, explanatory text `对登录的默认账号直接充值`, and bottom buttons `确认` / `返回`.
- verified: the recharge-page branch still does not escape the armed mode-1 state. Throughout the forced `choiceObj2` windows (`tick=132`, `196`, `225`, `238`, `293`, `333`, `344`), `sub_840_mode1_call_cbA` runs with `modeObjPtr=0501f808` while `obj2C=1` and `local10344=1` remain unchanged.
- verified: `choiceObj2` is not a terminal sink; it has at least two local transitions back out:
  - `lr=0501c125`: `0501f808 -> 0501f820` (recharge page back to the four-choice object)
  - `lr=0501c11b`: `0501f808 -> 05010db0` (recharge page into `choiceObj1/login_form`)
- implication: the four-choice branch is no longer "new login form vs old login screen". It is "login form vs recharge selection", and the recharge branch still inherits the same pre-armed `obj2C/local10344=1` mode-1 contract instead of clearing it.
- evidence: `bin/logs/net_trace.log` lines around `tick=115/158/218/271/418` for `force_title_choice2_install`, `tick=132/196` for `0501f808 -> 0501f820`, `tick=241/361` for `0501f808 -> 05010db0`, plus the user's direct screenshot of the forced page.

## 2026-06-05
- changed: replaced the temporary `choiceObj2` forced-install build with a new direct-jump validation build aimed at the already recovered `target50` role-list success family. When `title_four_choice_screen_handle_action()` reaches the normal `choiceObj1` install site (`LR=0501938B`), the emulator now substitutes `target50` (`base+10808`) into `sub_1048FE0()` and also forces the composite mode word `base+10748 = 1`.
- purpose: validate whether the sibling `+0x50` local target can be reached directly from the current `010534b4` path and whether the role-list/server-family UI can be brought up at all without solving the whole `99/1` contract first.
- validation: rebuilt successfully with `make`; this is another explicit experiment build and should be interpreted separately from both the natural path and the earlier forced-recharge-path build.

## 2026-06-05
- verified: the direct-jump `target50` experiment does install the target-family object. In the new rerun, `force_title_target50_role_install` fires at `tick=337`, then `mode_obj_set_ptr` switches the active object to `newPtr=0501f850` with callback triple `0501ae61/0501ae89/0501b2c5`.
- verified: that alone is still insufficient to keep the local composite screen in role mode. In the same window, the forced `mode=1` write is immediately followed by local writes that reset `mode -> 0`, `sel0 -> 0`, `sel1 -> 0`, and keep `roleFamily=0`.
- implication: the remaining gap has narrowed again. `target50` is a real reachable object family, but it appears to run initialization or fallback logic that immediately returns the composite state to server/idle mode when the expected backing role/server data is absent.
- changed: added a narrow watcher layer for exactly that reset path. The current build now traces:
  - `0501ae60/0501ae88/0501b2c4` as the active `target50` callback-family entries
  - `0501a6b8/0501a6c4/0501a6ce` as the observed local `mode/sel0/sel1` reset sites
  - `0501c770` as the observed `roleFamily` clear site
- purpose: the next rerun should answer whether `0501f850` itself actively resets the composite state, or whether a deeper helper discovers missing backing data and forces the fallback.

## 2026-06-05
- verified: the reset-path rerun resolves the main ambiguity. Runtime `0501a6b8/0501a6c4/0501a6ce` relocate to static `0x3AE8/0x3AF4/0x3AFE`, which are all inside `role_manage_screen_init()`, not a later failure-only fallback helper.
- verified: the forced `target50` object `0501f850` is the title-side role-manage family. Its callback triple relocates to:
  - `0501ae61 -> 0x4290 -> role_manage_screen_dispatch_mode_input()`
  - `0501ae89 -> 0x42B8 -> role_manage_screen_handle_input()`
  - `0501b2c5 -> 0x46F4 -> role_manage_screen_render()`
- verified: the first `target50` install window already shows `modeObjPtr=0501f850` while `listPtr=0`, `roleFamily=0`, `obj2C=1`, and `local10344=1`; `role_manage_screen_init()` then immediately resets `mode -> 0` and `sel0/sel1 -> 0` as part of its normal setup.
- correction: the `0501c770` trace label is broader than first assumed. Static recovery maps it to `sub_5B88()`, a generic zero-fill helper used during screen initialization, so it should not be read by itself as a role-family-specific fallback decision.
- implication: the current `target50` experiment already proves the role-manage family is installable, but the remaining blocker is no longer “why does a fallback clear mode”; it is “what backing role/server state should exist before `role_manage_screen_init()` so the newly installed role-manage object can progress beyond its normal zeroed startup state”.
- evidence: `bin/logs/net_trace.log` around `tick=150` (`force_title_target50_role_install`, `target50_reset_mode0`, `trace_title_login_write label=mode`), plus static decompile of `role_manage_screen_init()` / `role_manage_screen_dispatch_mode_input()` / `role_manage_screen_handle_input()` / `role_manage_screen_render()` in `mmTitleMstarWqvga.cbm`.

## 2026-06-05
- changed: widened the `target50` watcher to expose the role-manage family's backing state directly. `trace_title_target50_path` now includes:
  - `slotLimit` from `base+10737`
  - `initDone` and `uiWord56/58/60/62/64` from `base+10754..10764`
  - `activeRoleIndex` from `base+11124`
  - `roleCount` plus the first two raw tag bytes at `*base+10788`
- changed: mirrored the same fields into `trace_title_login_write` so the next rerun can catch live writes to the role-manage setup words, not just their sampled values at `target50` entry.
- purpose: the next rerun should answer whether the install failure is simply "workspace count is zero", "activeRoleIndex is out of range", or a different role-manage startup precondition altogether.

## 2026-06-05
- verified: the widened `target50` rerun answers that backing-state split directly. At the forced install window (`tick=121`), the role-manage object is entered with:
  - `listPtr=0`
  - `roleCount=0`
  - `tag=0,0`
  - `activeRoleIndex=0`
  - `slotLimit=0`
  - `initDone=0`
  - `uiWord56/58/60/62/64 = 0/0/0/0/0`
- verified: those values remain zero through `target50_reset_mode0`, `target50_reset_sel0`, and `target50_reset_sel1`; only the ordinary `mode -> 0` reset from `role_manage_screen_init()` is observed.
- implication: the current forced-install path is not failing because of a subtle index mismatch. It is entering `role_manage_screen_init()` with an entirely empty role-manage backing workspace, so the role-manage family never gets past its normal zero-state startup.
- evidence: `bin/logs/net_trace.log` lines around `tick=121` for `trace_title_target50_path label=target50_clear_roleFamily`, `target50_reset_mode0`, and the matching `trace_title_login_write label=activeRoleIndex`.

## 2026-06-05
- verified statically: the missing workspace is not something `target50` allocates for itself. In `mmTitleMstarWqvga.cbm`, `role_list_screen_init()` allocates and zeroes the `base+10788` workspace (`19236` bytes), stores the pointer to `base+10788`, seeds per-slot UI objects, and sets `state0=1` / ready byte `base+11046=1`.
- verified statically: `title_handle_role_list_response()` is the confirmed network-fed filler for that same workspace. On `type=1/subtype=4`, it reads the role count into `*(*base+10788)`, fills repeated role records under the workspace, then calls `title_transition_router_invoke_default_target()`.
- implication: the current `target50` force-jump is bypassing the whole role-list seeding contract. The empty `listPtr=0` seen at `role_manage_screen_init()` is now best explained by the fact that neither `role_list_screen_init()` nor `title_handle_role_list_response(1/1/4)` has run on the current path.
- changed: added a new read-only watcher family for that upstream contract:
  - `role_list_screen_init`
  - `title_handle_role_list_response`
  - `title_transition_router_invoke_default_target`
  It logs `listPtr`, `roleCount`, role-list ready byte, the active mode object, and a couple of representative workspace fields so the next rerun can show whether the title path ever seeds the role-list family before the later `target50` experiment.
- validation note: because the user's current `bin/main.exe` was still locked by the last run, this watcher build was linked as `bin/main_roletrace.exe` instead of overwriting `bin/main.exe`.

## 2026-06-05
- verified: the rebuilt `main.exe` with the new role-list watcher still shows no `trace_title_rolelist_path` hits at all on the current login/title run. None of the three upstream probes fire:
  - `role_list_screen_init`
  - `title_handle_role_list_response`
  - `title_transition_router_invoke_default_target`
- verified: the same rerun also shows no `1/1/16` request or `1/1/4` reply in the packet/log stream before the later forced `target50` substitution.
- implication: the present path is not “reaching role-list seeding and then leaving the workspace empty”. It is skipping the entire role-list contract before the `target50` experiment point. The empty `listPtr=0` at `role_manage_screen_init()` is therefore a consequence of the upstream branch never entering the role-list family in the first place.
- evidence: absence of any `trace_title_rolelist_path` lines in `bin/logs/net_trace.log`, plus absence of `1/1/16` / `1/1/4` markers in the same session’s packet/trace logs.

## 2026-06-05
- changed: replaced the temporary direct-jump target again. The current validation build no longer substitutes `target50`; it now substitutes `target4c` at the same `title_four_choice_screen_handle_action()` install site.
- change detail:
  - old experiment: `choiceObj1 -> target50` and force `mode=1`
  - new experiment: `choiceObj1 -> target4c` only, with no forced write to `base+10748`
- purpose: validate the earlier role-list family directly, without contaminating the path with the later role-manage local mode contract. The next rerun should look for `force_title_target4c_rolelist_install`, then see whether `role_list_screen_init`, `role_list_screen_render`, or the `1/1/16 -> 1/1/4` exchange finally appears.
- validation: rebuilt successfully with `make`.

## 2026-06-05
- verified: the `target4c` direct-jump experiment does enter the earlier role-list family. Runtime now shows `force_title_target4c_rolelist_install`, immediate `mode_obj_set_ptr ... newPtr=0501f838`, and a direct hit on `trace_title_rolelist_path label=role_list_screen_init`.
- verified: the role-list family then emits a real outbound `1/1/16` request on the next tick. The request appears as `send_payload len=9 ... hex=575400090101100005`, i.e. top-level `1/1/16`, and the same session logs `unhandled_packet WT len=9 hdr=1/16 objs=1/1/16 count=1`.
- implication: the current blocker has moved again. We no longer need to guess whether `target4c` is the right upstream family; it is. The missing emulator/server contract is now the response side of that `1/1/16` exchange, which should eventually drive `title_handle_role_list_response()` and seed the `base+10788` role-list workspace before any later `target50` / role-manage path can work.
- evidence: `bin/logs/net_trace.log` around `tick=169..170` for `force_title_target4c_rolelist_install`, `trace_title_rolelist_path label=role_list_screen_init`, `send_call connect=2 len=9`, and `unhandled_packet ... hdr=1/16`; matching `bin/logs/net_packets.log` `send_payload ... hdr=1/16`.

## 2026-06-05
- changed: wired a minimal live handler for the newly confirmed `1/1/16` role-list request into `src/main.c`. The current build now answers top-level `1/1/16` with a two-object WT packet:
  - `1/1/16 { result = 1 }`
  - `1/1/4 { actorinfo = counted title-side role-entry table }`
- rationale: static `title_handle_role_list_response()` first caches subtype-16 `result` into ready byte `+11046`, then while that byte is `1` it parses subtype-4 `actorinfo` into the `base+10788` workspace and finally invokes the default transition router. `servconf` stays omitted for now because the parser treats it as optional.
- validation: rebuilt successfully with `make`. The next rerun should confirm whether this minimal stage response is enough to hit `trace_title_rolelist_path label=title_handle_role_list_response` and populate `listPtr/roleCount`.

## 2026-06-05
- verified: the new live `1/1/16` handler does fire on the forced-`target4c` path. Runtime now shows `mock_default source=builtin-title-rolelist-stage len=73`, then two direct hits on `trace_title_rolelist_path label=title_handle_role_list_response`, followed by `trace_title_rolelist_path label=title_transition_router_invoke_default_target`.
- verified: this is the first run where the earlier role-list family is confirmed end-to-end through its local response parser. At router time the trace shows `ready=1`, `listPtr=05400000`, and `roleCount=1`, proving the minimal `16(result=1) + 4(actorinfo)` reply is sufficient to drive `title_handle_role_list_response()`.
- verified: the same window also shows subsequent local seeding for the later family: `initDone -> 1`, `slotLimit -> 2`, resource loads for `UI10/UI14/UI16/UI20/updown.gif`, and a later `target50_cbC_enter` with `modeObjPtr=0501f850` and `listPtr=0501fa50`.
- implication: the active gap has narrowed again. We are no longer blocked on entering role-list or on satisfying `title_handle_role_list_response()`. The next problem is the handoff/steady-state after that router callback: the title path still remains on `activeScreen=010534b4` with `mode=0`, `obj2C/local10344=1`, and the later target50-side trace still reports `roleCount=0`.
- evidence: `bin/logs/net_packets.log` line `tick=376 source=builtin-title-rolelist-stage responseLen=73`; `bin/logs/net_trace.log` lines for `title_handle_role_list_response`, `title_transition_router_invoke_default_target`, `trace_title_login_write label=initDone`, `trace_title_login_write label=slotLimit`, `trace_title_login_state ... listPtr=05400000 listCount=2`, and `trace_title_target50_path label=target50_cbC_enter`.

## 2026-06-05
- clarified: the title login entry is now statically split into two different request/owner families, not one login request with interchangeable field spellings.
- verified:
  - `title_four_choice_screen_handle_action()` sends `net_build_login_request(1, 12, 5)` when `choiceSel == 0`
  - `login_form_submit()` sends `net_build_login_request(1, 1, 6)` on explicit account/password confirm
  - `net_build_login_request()` uses different credential sources on those branches: subtype `1` writes `userName` from live edit buffers, while subtype `12` writes `username` from saved `mmorpg_LoginRecord`
  - the response owners are also distinct: `1/1/12 -> login_alt_response_dispatch_wrapper() -> login_stage_success_dispatch()`, `1/1/1 -> login_primary_response_dispatch() -> login_response_result_dispatch()`
- implication: if the emulator answers a live `1/1/12` request with the `1/1/1` primary-success contract, or lets a later side-stage packet dominate the window, the client may still appear to "accept a login response" while being routed into the wrong local target family. The highest-confidence path for the expected server-list selection UI remains the explicit login-form `1/1/1` branch with `serverinfo/servernum/newVer`.
- changed: narrowed the queued `1/1/15 {result=1}` follow-up in `src/main.c` so it now only attaches to alternate `1/1/12` requests. The explicit primary `1/1/1` login-form path can now be rerun as a cleaner control case.
- verified: that clean control rerun is now complete and still negative. The live session shows only the primary pair:
  - request `hdr=1/1` with `userName/password/imsi`
  - response `mock_login_primary_validation_response top=1,1,1 servernum=1`
  - no queued `1/1/15` follow-up in the same window
- verified: despite that clean wire pair, the response is still delivered under `dispatch=05017509`, and `trace_title_login_state` remains unchanged across `pre/post_data_event_05017509` (`mode=0`, `listPtr=0`, `roleFamily=0`, `activeScreen=010534b4`).
- implication: removing the old `1/1/15` side-stage contamination is not sufficient to recover the expected server-list flow. The remaining blocker is now better framed as an owner/target handoff problem before primary success delivery, not as a missing late follow-up packet after delivery.

## 2026-06-05
- changed: documented the confirmed local scene-resource wrapper and added a first standalone exporter script at `tools/export_map_bmp.py`.
- verified: `JHOnlineData/*.sce` / `*.map` / `*.actor` resources currently split into two confirmed wrapper families: `type=1` (`len_le32` + `0x01` + custom GIF payload for `gifDecodeExt`) and `type=2` (`len_le32` + `0x02` + `be32 compLen` + `be32 outLen` + custom LZSS stream). The `.sce` sample `c00蓬莱仙岛_01.sce` expands to `SCE2` and embeds `00蓬莱仙岛_01.map`; the `.map` sample expands to `u32 imageCount`, `len+name` image list, `u32 mapWidth/mapHeight/tileWidth/tileHeight`, then a dword cell table.
- verified: for the currently tested map family, the practical render rule that reproduces coherent scene BMPs is `imageIndex = (cell >> 24) & 0x0F`, `tileIndex = cell & 0xFF`, with `imageIndex` selecting the decoded `m_*.gif` atlas and `tileIndex` selecting a row-major `16x16` tile inside that atlas. The upper nibble `cell >> 28` is now recorded as an unresolved flag field; it is not needed to render most tested maps, but two palace-style samples still produce out-of-range tile ids and remain partial.
- evidence: IDA decompile of `sub_100D564`, `vm_IMG_CreateImageFormStream`, and `src/gifDecode.c`; Python validation against `00蓬莱仙岛_01.map`, `00蓬莱仙岛_02.map`, `00蓬莱仙岛_03.map`, `01桃花岛_01.map`, `04临安府_01.map`; visual check of provisional BMP exports from the new script.

## 2026-06-05
- corrected: the first exporter pass had the `.map` cell table transposed. The loader stores cells as `cells[col][row]`, not row-major `cells[row][col]`, and the render path indexes them in that same order.
- verified: `sub_100D564()` reads `cols = div(mapWidth, tileWidth)` into `a1+16`, `rows = div(mapHeight, tileHeight)` into `a1+20`, allocates an outer pointer table of `cols`, then for each outer slot allocates an inner dword array of `rows` and fills it directly from the stream. `sub_10053A4()` later indexes `a1+24[firstIndex][secondIndex]`, where the outer loop advances by tile width / X and the inner loop advances by tile height / Y. This matches `cells[col][row]` storage.
- corrected: the same exporter pass also truncated the frame selector to `cell & 0xFF`; the draw path actually strips only the top 8 bits and uses the full low 24 bits as the tile/frame ordinal.
- validation: after switching to column-major lookup and `tileIndex = cell & 0x00FFFFFF`, `01桃花岛_01.map`, `04临安府_01.map`, and `00蓬莱仙岛_01.map` all export into coherent layouts instead of the earlier scrambled/repeated patterns.

## 2026-06-05
- verified: the ordinary on-disk `JHOnlineData/*.actor` resources are a separate type-2 descriptor family parsed by `sub_100DB82()`, not the synthetic `0xF1` motion-wrapper shape used in the earlier mock experiment.
- verified: the common type-2 `.actor` payload layout is:
  - `u32 imageCount`
  - `imageCount * (u8 len + ascii imageName)`
  - `u32 rectCount`
  - `rectCount * { i32 left, i32 top, i32 right, i32 bottom, i32 imageIndex }`
  - `u32 animationCount`
  - `animationCount * { u32 partCount, partCount * { u32 partId, u32 frameCount, frameCount * { i32 rectIndex, i32 offsetX, i32 offsetY, i32 unk3, i32 unk4 } } }`
- verified: `sub_100DB82()` stores the rectangle records as `10-byte` entries derived from two corners (`w = right - left`, `h = bottom - top`) and stores the nested animation records as `8-byte` part headers plus `10-byte` frame entries. In the current asset set, `frame.unk3` is always `1` and `frame.unk4` is always `0`.
- evidence: static decompile/disassembly of `sub_100DB82()` plus parse validation across all tracked type-2 actor samples in `bin/JHOnlineData`: `130` ordinary `.actor` files parse cleanly with this schema, while only the two mock files `c_mock_missing_motion.actor` / `mock_missing_motion.actor` stay on the synthetic `type=0xF1` path.
- changed: added `tools/inspect_actor.py` so ordinary `.actor` files can now be dumped into JSON directly from the repository without redoing the format recovery by hand.

## 2026-06-05
- changed: added `tools/export_actor_gif.py` to render ordinary type-2 `.actor` resources into animated GIFs by compositing their referenced `*.gif` atlases and rectangle table.
- verified: the raw `.actor` records can be projected into at least two useful visualizations:
  - `mode=animations`: one GIF per outer animation entry, where each inner entry is treated as a fully composited frame/layer set
  - `mode=tracks`: one GIF per repeated `part_id`, stitched across outer animation entries in file order
- evidence: sample exports now render plausibly for both object and character actors, e.g. `b_bamboo.actor` as a static assembled plant sprite and `h_warrior.actor track00` / `boss09.actor track00` as coherent character/monster composites. The exact gameplay meaning of the outer animation index versus repeated `part_id` is still not fully confirmed, so the exporter intentionally keeps both projections available instead of hard-coding one interpretation as final.

## 2026-06-05
- verified: there are no tracked `.scene` files in `bin/JHOnlineData`; the scene-container files on disk are `.sce`, and they use the same outer type-2 resource wrapper as `.map` / ordinary `.actor`.
- verified: the `.sce` payload starts with `SCE2`, followed by two little-endian header coordinates/size-like values and a length-prefixed primary `.map` name. Example: `c04临安府_01.sce` decodes to `header_x=304`, `header_y=416`, `header_flag=1`, `map_name=04临安府_01.map`.
- verified: beyond the header, `.sce` stores scene-content references rather than tile pixels. Recovered samples consistently contain:
  - references to other `.sce` files, which behave like scene exits / transitions
  - in-scene `.actor` references for NPCs or interactive props
  - matching `.xse` names for many of those scene objects
  - GBK display names for portals and NPCs
- evidence: direct payload inspection of `c00蓬莱仙岛_01.sce`, `c00蓬莱仙岛_03.sce`, `c04临安府_01.sce`, and `c14蜀山_01.sce`; e.g. `c04临安府_01.sce` includes `04临安府_01.map`, exits to `c04临安府_05.sce` and others, plus NPC records such as `n_solider2.actor` + `04临安宋兵乙.xse` + `宋兵乙`.
- changed: added `tools/inspect_sce.py`, a heuristic scene inspector that decodes the header, lists `.map/.sce/.actor/.xse` references, and groups nearby actor/xse/display-name triples into a practical scene summary. Its grouping is intentionally heuristic; exact binary field ids inside the post-header object table are still not fully named.

## 2026-06-05
- changed: upgraded `tools/inspect_sce.py` from the earlier string-only heuristic into a structured scene parser plus static renderer.
- verified: the leading scene section is now formalized as `kind=1, version=1` plus a prop-scatter block. The common decorative branch uses `placement_count`, `scatter_group=1`, `reserved=0`, `template_count`, then `template_count` `.actor` names and `placement_count * {templateIndex, x, y, flags}`; `c00蓬莱仙岛_01.sce` parses as `2` prop templates (`b_bamboo.actor`, `b_flowers01.actor`) and `7` placements, and the rendered preview places those props coherently on top of the exported map.
- verified: the most common portal families are now named and parsed structurally:
  - edge portals with a destination point plus `field6=.sce`, `field7=entryId`, `field0x0A..0x0D=triggerRect`, `field0x13=targetEntryId`
  - compact/meta portals that omit the destination point but keep the same `.sce + triggerRect + targetEntryId` tail
  - named/tile portals that use an inline trigger-tile rectangle plus `field0x16=displayName` and `field0x17=targetScene`
- verified: ordinary scene actors are now parsed as records led by `field3=.actor`, with optional `field4=.xse`, optional `field2=stateText`, optional `field1=displayName`, and a short numeric trailer that often resolves into an on-map position pair. This is enough to render representative town scenes such as `c04临安府_01.sce` and `c14蜀山_01.sce` with their NPC/prop actors composited over the map.
- validation: representative renders now exist at `tmp/c00_scene.png`, `tmp/c04_scene.png`, and `tmp/c14_scene.png`. A full batch parse over `185` `.sce` files succeeds structurally; only `b_01蓬莱仙岛.sce` still fails earlier at resource decompression, and many non-town scenes still leave some specialized combat/spawner records in `unknown_blob` for future refinement.

## 2026-06-05
- changed: finished the next owner-handoff watcher in `src/main.c` / `src/hookRam.c`. `hookRamCallBack()` now also traces writes overlapping `g_netCurrentObject + 0x14`, and logs them as `trace_current_net_object_write`.
- purpose: the clean primary `1/1/1` control already proved that `result=1 + serverinfo + servernum + newVer` still lands while `dispatch=05017509/sub_938` owns delivery. This new watcher is meant to answer the narrower remaining question: which code path installs that shared callback, and whether any earlier local/title transition ever tries to replace it before primary success arrives.
- validation: rebuilt with `make` after wiring the watcher; build passed.

## 2026-06-05
- verified: the first `trace_current_net_object_write` rerun finally pins the active-owner handoff sequence around the stuck primary `1/1/1` path.
- verified: there are exactly two meaningful writes to `g_netCurrentObject + 0x14` before the clean primary login reply arrives:
  - at `tick=68`, `last=0103BB36`, the main-CBE helper `sub_103BAFA()` clears `*(* (R9+39236) + 20) = 0`
  - immediately afterwards, still at `tick=68`, `last=050176C2`, the title-module init path `sub_A50()` writes the same callback slot to `05017509`, i.e. relocated `sub_938 + 1`
- verified: after that install there are no later rewrites before either clean primary `1/1/1` success window. Both login submits log `business_cb_existing obj=010560f0 cb=05017509 reason=login-request`, and both subsequent `pre/post_data_event_05017509` snapshots remain unchanged.
- implication: the current failure is no longer best framed as “some later code switched the owner away from the primary handler”. On this path, the shared callback slot is explicitly reset by main-CBE startup glue and then claimed by `mmTitleMstarWqvga.cbm::sub_A50()` during `010534b4` screen initialization; nothing later hands it off again before the primary `1/1/1` success lands.
- evidence: `bin/logs/net_trace.log` lines for `trace_current_net_object_write` at `tick=68`, the later `business_cb_existing ... cb=05017509 reason=login-request` lines, and static IDA for `江湖OL.CBE::sub_103BAFA()` (`0x0103BAFA`, write at `0x0103BB36`) plus `mmTitleMstarWqvga.cbm::sub_A50()` (`0x0A50`, relocated write site at runtime `050176C2`).

## 2026-06-05
- verified: the outer stuck title screen `010534b4` is created directly by `mmTitleMstarWqvga.cbm::sub_C82()`. The live add-site `last=050178C8` maps back to static `0x0CF8` inside `sub_C82`, exactly where it invokes the manager method at `*(R9+0x203C)+0x10` after `sub_FFA()`, `sub_C54()`, `sub_1114()`, and `title_register_local_screens()`.
- verified: the same static pass narrows the local child-screen split:
  - `choiceSel == 0` -> `net_build_login_request(1, 12, 5)`
  - `choiceSel == 1` -> install `choiceObj1` (`login_form_screen`)
  - `choiceSel == 2` -> install `choiceObj2` (the recharge/login variant)
  - `choiceSel == 3` -> call the role-list family's `+0x18` continuation hook, then `sub_EC(9)`
- implication: there is still no second outer title constructor in this module's recovered local graph; `sub_C82()` is the one visible outer title transition screen, and the interesting remaining branch is now inside its child-screen/continuation setup rather than at a later “mystery owner overwrite”.
- changed: extended `trace_title_login_state` so the next rerun will print the active child object pointer plus candidate local object callback slot `+0x14` values (`modeObj`, `choiceObj1`, `choiceObj2`, `target48/4c/50/54`). The immediate goal is to confirm at runtime that the installed login-form child already carries `login_primary_response_dispatch()` while the shared owner slot still stays on `sub_938`.
- validation: rebuilt with `make` after adding the widened title-state trace; build passed.

## 2026-06-05
- verified: the widened title-state trace disproves the simple “active child object already carries `0x2D80` in `+0x14`” guess. On the clean primary `1/1/1` rerun:
  - before selection, `modeObj=0501f820` (four-choice) but `modeObj.cb14 = 0`
  - after switching into the login form, `modeObj=05010db0` with the expected UI callbacks at `cb0/cb4/cb8/cb10`, but `modeObj.cb14` is still `0`
  - the same is true for `choiceObj1`, `choiceObj2`, and `target48/4c/50/54`: every sampled object's `+0x14` field stays `0`
- implication: the local screen network callback registered by `title_register_local_screens()` is not stored in the child object instances at the naive `+0x14` slot we just sampled. That means the earlier static owner split (`login_form -> login_primary_response_dispatch`, `role_list -> title_handle_role_list_response`, etc.) is still valid, but the callback must live in manager/registration metadata rather than directly on the active child object payload.
- evidence: `bin/logs/net_trace.log` lines for `trace_title_login_state` at `tick=118` (`modeObj=0501f820`) and `tick=1160/1169` (`modeObj=05010db0`) showing `cb14=00000000` across the active child, `choiceObj1`, `choiceObj2`, and `target48/4c/50/54`; paired static IDA still shows `title_register_local_screens()` registering `login_primary_response_dispatch`, `title_handle_role_list_response`, and `role_manage_screen_handle_network`.

## 2026-06-05
- corrected: the previous child-object callback guess was off by one slot family. Static main-CBE helper recovery now shows the local child manager at `base+10444` is built by `sub_1048FEE()` with methods:
  - `+0x08 -> sub_1048F8C` (call active child `+0x04`)
  - `+0x0C -> sub_1048FB2/sub_1048FB4` (call active child `+0x08`)
  - `+0x10 -> sub_1048FCA` (call active child `+0x10`)
  - `+0x14 -> sub_1048FE0` (replace active child pointer and call child `+0x00`)
- implication: on the current title path, the most likely local-screen network dispatch route is manager `+0x10 -> child +0x10`, not any direct child `+0x14` field. The login-form child's sampled `cb10` value (`05019951`) already matches relocated `login_primary_response_dispatch + 1`, while the shared business owner still remains `05017509/sub_938`.
- changed: added a new runtime watcher `trace_title_child_manager_call` at main-CBE `0x1048FCA` (`manager_call_cb10`) and `0x1048FB4` (`manager_call_cb8`). The next rerun should reveal whether the shared title/business path ever forwards a packet into the active child manager, and if so whether `1/1/1` is being passed to the login-form child's `cb10`.
- validation: rebuilt with `make` after adding the manager-call watcher; build passed.

## 2026-06-05
- verified: the first `trace_title_child_manager_call` rerun answers the biggest remaining owner question. The clean primary `1/1/1` reply is not being dropped before the child layer; while shared owner `05017509/sub_938` remains active, it **does** forward the packet into the local child manager's `+0x10` path.
- verified: both primary login windows now show:
  - `business_cb_existing ... cb=05017509 reason=login-request`
  - then at delivery time `trace_title_child_manager_call label=manager_call_cb10 ... child=05010db0 ... cb10=05019951 packetKind=1 packetSubtype=1`
- implication: the earlier framing “`1/1/1` never reaches the primary owner” is now too broad. The packet already reaches the active `login_form` child callback (`login_primary_response_dispatch + 1`) through the outer `sub_938` owner path; the remaining blocker has moved inside the primary wrapper/result-dispatch chain itself.
- changed: added one more narrow runtime watcher for the relocated primary dispatch family:
  - `05019950` -> `login_primary_dispatch_entry`
  - `05018F90` -> `login_result_dispatch_entry`
  - `05018FA4` -> `login_result_dispatch_success`
  - `05018FB8/05018FC2` -> failure branches
  It logs packet `kind/subtype`, current parsed `resultByte`, and the current `target48/4c/50` slots as `trace_title_login_dispatch`.
- validation: rebuilt with `make` after adding the primary-dispatch watcher; build passed.

## 2026-06-05
- changed: added a scene-bundle export mode to `tools/inspect_sce.py` via `--bundle-root`. In this mode each scene is written to `<bundle-root>/<scene-file-name>/` and now includes:
  - `scene.json`
  - `scene.png`
  - `map.base.png`
  - `map_sources/*.png` for the decoded map atlases used by the scene
  - `actor_previews/*.png` for the actor composites reused during static scene rendering
- validation: a full rerun to `tmp/all_sce_bundle` produced `185` successful scene bundles and the same single pre-existing decompression failure on `b_01蓬莱仙岛.sce`.

## 2026-06-05
- verified: the newest clean-primary rerun moves the blocker inside the primary result dispatcher itself. Runtime now shows:
  - `trace_title_child_manager_call label=manager_call_cb10 ... child=05010db0 ... cb10=05019951 packetKind=1 packetSubtype=1`
  - `trace_title_login_dispatch label=login_primary_dispatch_entry ... resultByte=1`
  - `trace_title_login_dispatch label=login_result_dispatch_entry ... resultByte=1`
  - but no `trace_title_login_dispatch label=login_result_dispatch_success`
- verified: this matches the current mock payload shape. `bin/logs/net_packets.log` still shows `result` encoded as raw value byte `01` inside the `1/1/1` response (`...726573756c740003000101...`), while static `login_response_result_dispatch()` / `net_handle_login_response()` compare against ASCII digits (`'1'`, `'2'`, ...), not numeric `1/2/...`.
- changed: updated the built-in login-family mock builders so the `result` field is encoded as ASCII digits on the title-login branches that are statically confirmed to use character comparisons:
  - primary `1/1/1` success
  - primary `1/1/1` failure
  - alternate `1/1/12` success
  - queued title `1/1/15` success
- unchanged: numeric `result=1` for the live role-list `1/1/16` path is intentionally left alone, because `title_handle_role_list_response()` checks it as a numeric ready byte rather than an ASCII digit.
- validation: rebuilt with `make` after the encoding change; build passed.

## 2026-06-06
- verified: the ASCII-digit fix works. The latest clean primary rerun now shows:
  - `trace_title_login_dispatch label=login_result_dispatch_entry ... resultByte=49`
  - `trace_title_login_dispatch label=login_result_dispatch_success`
  - immediate local transition from `modeObj=05010db0` to `modeObj=0501f838`
  - `listPtr` seeded to `05400000`
- verified: the next live client action after primary success is now a real outbound `1/1/16` request (`WT len=9 hdr=1/16 objs=1/1/16 count=1`), which matches the earlier role-list-family recovery.
- corrected: on the current rolled-back branch that live `1/1/16` request was still unhandled, so the run stopped at `assert(0)` before `title_handle_role_list_response()` could be exercised again.
- changed: restored a minimal built-in `1/1/16` stage response on the live path:
  - `1/1/16 { result = 1 }`
  - `1/1/4 { actorinfo = counted title role-list table }`
  This matches the already recovered `title_handle_role_list_response()` contract and is intentionally kept separate from the primary `1/1/1` success packet.
- validation: rebuilt with `make` after restoring the handler; build passed.

## 2026-06-06
- verified: the restored live `1/1/16` stage is now consumed on the clean primary path. Runtime shows:
  - `mock_default source=builtin-title-rolelist-stage`
  - then two child-manager `cb10` dispatches into the active `target4c` family object `0501f838`, first for `packetSubtype=16`, then for `packetSubtype=4`
  - after that event, the active local object changes from `0501f838` to `0501f850`
- verified: this means the current mock progression `1/1/1(primary success, ASCII result) -> 1/1/16 -> 1/1/4` is semantically accepted by the real title-local chain. The path no longer stalls at login success and no longer dies on unhandled `1/1/16`.
- observed: the post-stage local state is still not the expected visible server-select result. After the `16+4` event:
  - `activeScreen` remains `010534b4`
  - `modeObj` becomes `0501f850` (the already recovered `target50` role-manage family)
  - `listPtr` stays nonzero (`05400000`)
  - sampled `listCount` becomes `2`
  - but `mode=0`, `roleFamily=0`, and `sel0/sel1=0`
- implication: the remaining mismatch is now later and narrower than before. The current packet family is sufficient to drive the client into `target4c -> target50`, but the local workspace/content still does not produce the expected stable server/role selection UI state.
- changed: added a tighter `trace_title_role_path` watcher around the relevant role-list / role-manage family callbacks:
  - `target4c_handle_network` (`0501A114`)
  - `target50_init` (`0501A6A2`)
  - `target50_dispatch_mode_input` (`0501AE60`)
  - `target50_handle_input` (`0501AE88`)
  - `target50_render` (`0501B2C4`)
  - `target50_handle_network` (`0501BFBC`)
  It samples `ready`, `slotLimit`, `initDone`, `activeRoleIndex`, `listPtr`, `roleCount`, workspace tag bytes, current `mode/sel`, and the active `modeObj` callback triple.
- validation: rebuilt with `make` after adding the watcher; build pending next rerun.

## 2026-06-06
- verified: the first `trace_title_role_path` rerun resolves the immediate `target4c -> target50` entry conditions.
- verified at `target4c_handle_network` on the same `event=7`:
  - both subtype `16` and subtype `4` are dispatched into the active `0501f838` object
  - `ready=1`
  - `listPtr=05400000`
  - but sampled workspace state is still mostly zero at that instant: `roleCount=0`, `slotLimit=0`, `initDone=0`, `activeRoleIndex=0`, `roleFamily=0`
- verified at `target50_init` immediately afterwards:
  - the active object has switched to `0501f850`
  - `roleCount` is already `1`
  - but `mode=0`, `slotLimit=0`, `initDone=0`, `activeRoleIndex=0`, and `roleFamily=0` remain unchanged at init entry
- verified by the end of the same event / first render:
  - `initDone -> 1`
  - `slotLimit -> 2`
  - `ui56/58/60/62/64 -> 48/47/68/56/5`
  - sampled `listCount -> 2`
  - `target50_render` sees `ready=1`, `roleCount=2`, `slotLimit=2`, `initDone=1`
  - but still keeps `mode=0`, `sel0/sel1=0`, `activeRoleIndex=0`, and `roleFamily=0`
- implication: the remaining blocker is no longer “empty workspace” and no longer “`1/1/16` contract missing”. By first `target50_render`, the role-manage family already has a seeded workspace plus valid `ready/initDone/slotLimit`; the narrower mismatch is that it still chooses to remain in submode `0` / `roleFamily=0` instead of transitioning into the expected visible selection state.

## 2026-06-06
- corrected: `target50`'s observed `mode=0` should no longer be treated as a generic "zero/failure" state. Static recovery of `role_manage_screen_dispatch_mode_input()` shows:
  - `mode == 0` dispatches to `role_manage_screen_handle_role_list_nav()`
  - `mode == 1` dispatches to `role_manage_screen_handle_action_menu_input()`
  - `mode == 2` dispatches to `role_manage_screen_handle_create_name_input()`
- corrected: `roleFamily=0` is also a valid live family/tab selection, not proof of missing initialization. Static `role_manage_screen_handle_input()` uses `roleFamily` as a three-way selector (`0/1/2`) and writes a new family value when the user taps a different panel; when the family stays `0` it additionally enables left/right navigation hotspots for the first tab.
- implication: the latest good primary chain (`1/1/1 -> 1/1/16 -> 1/1/4`) is now entering `target50` in what looks like its normal initial role-list navigation submode. The remaining gap is more likely the semantic contents of the seeded role workspace or a later visual/input expectation, not the mere fact that `mode` and `roleFamily` are zero.

## 2026-06-06
- verified: `role_manage_screen_handle_role_list_nav()` (`0x3ECE`) gives the concrete meaning of the earliest `target50` workspace fields:
  - `base+10750` = current slot within the visible page
  - `base+10752` = page/window offset
  - `*(*(base+10788))` = role-count byte in the seeded workspace
  - `base+10737` = slotLimit / visible-capacity bound
- verified: in `mode=0`, local inputs behave as:
  - `0x20000` / `4` = move selection left/up through `10750/10752`
  - `0x40000` / `256` = move selection right/down, bounded by `roleCount` and `slotLimit`
  - `0x20` / `0x4000` = confirm current slot; if `roleCount == 1`, jump straight into `role_manage_screen_enter_action_menu()`, otherwise call `role_manage_screen_select_role_slot(selectedIndex)`
  - `0x1000` = if the current selection is not the last slot (`selectedIndex != roleCount - 1`), switch to `mode=2` create-name flow
  - `0x2000` = call the local wrapper at `base+10464`, then `sub_722()` full-disarm/reset
- implication: the current post-`1/1/16 -> 1/1/4` state is not “stuck doing nothing”; it is sitting in the normal navigation handler and already has enough state to respond to role-list inputs. The next likely mismatch is the semantic contents/layout of the seeded role records, or a later confirm-path expectation inside `role_manage_screen_select_role_slot()` / `role_manage_screen_enter_action_menu()`.

## 2026-06-06
- verified: the confirm path on `target50` is now statically recovered too.
  - `role_manage_screen_select_role_slot(selectedIndex)` (`0x39FC`) treats `selectedIndex == roleCount - 1` as the special final slot and enters action-menu mode (`mode=1`) through `role_manage_screen_enter_action_menu()`.
  - otherwise it copies fields from the selected workspace record, stores a role identifier into both the role workspace side-buffer and `mmorpg_LoginRecord`, sets `mode=3`, and sends `net_build_login_request(1, 6, 1)`.
- implication: the latest good chain is already reaching a real role-list navigation state where confirm on a normal slot should advance to live `1/6/1` role-selection traffic, while confirm on the last slot should enter the local action-menu branch. This strengthens the hypothesis that the remaining mismatch is semantic role-record content / visual rendering, not a missing higher-level packet family.

## 2026-06-06
- changed: tightened `trace_title_role_path` so it now also samples the first seeded role record from `base+10788`:
  - `role0.id` from record `+104`
  - `role0.level` from record `+176`
  - `role0.sex` / `role0.job` from record `+324/+325`
  - `role0.name` from record `+72` (decoded as GBK for logging)
  - `selectedRoleIdShadow` from workspace `+2044`
- purpose: the next rerun should answer whether the post-`1/1/16 -> 1/1/4` workspace already contains semantically plausible role records, or whether `target50` is navigating a structurally valid but content-poor synthetic table.
- validation: rebuilt with `make` after the watcher extension; build passed.

## 2026-06-06
- verified from the new runtime samples that the seeded title-side role workspace is now mostly semantically plausible by the time `target50` starts:
  - `role0.id = 10001`
  - `role0.name = 侠剑江湖`
  - `role0.sex = 0`
  - `role0.job = 1`
- observed: two fields are still suspicious on the same first-render sample:
  - `role0.level = 0` even though the current mock builder intends to emit a nonzero role level
  - `selectedRoleIdShadow = 0`, i.e. the workspace-side selected-role shadow at `listPtr + 2044` is still unset
- implication: the role-list stage is no longer "empty/garbage", but the synthetic role record still looks incomplete. The next likely mismatch is within the seeded record semantics rather than the outer `1/1/1 -> 1/1/16 -> 1/1/4` packet family.

## 2026-06-06
- changed: added a dedicated `trace_title_role_workspace_write` watcher for the live role workspace behind `base+10788`.
- it now logs writes to:
  - `roleCount`
  - `role0.id`
  - `role0.level`
  - `role0.sex`
  - `role0.job`
  - `selectedRoleIdShadow`
  - plus a name snapshot whenever the first role-name range (`record+0x48..0x67`) is written
- purpose: the next rerun should distinguish "parser never wrote the suspicious fields" from "fields were written correctly and later overwritten/cleared by local role-manage logic".
- validation: rebuilt with `make` after adding the watcher; build passed.

## 2026-06-06
- verified: the new workspace watcher answers the `level/shadow` ambiguity directly on the clean primary chain.
- before the live `1/1/16 -> 1/1/4` event is consumed, the current workspace is explicitly cleared by local code around `last=0501c770/0501c774`, including:
  - `roleCount -> 0`
  - first role name range zeroed
  - `role0.id -> 0`
  - `role0.level -> 0`
  - `role0.sex/job -> 0`
  - `selectedRoleIdShadow -> 0`
- after `title_handle_role_list_response()` consumes the fresh `1/1/4.actorinfo`, the parser really does repopulate part of the first record:
  - `roleCount -> 1` at `last=0501a21c`
  - `role0.id -> 10001` at `last=0501a23a`
  - `role0.job -> 1` at `last=0501a24c`
  - `role0.sex -> 0` at `last=0501a258`
  - `role0.name -> 侠剑江湖` across `last=0501c818/0501c8c8`
- crucially, `role0.level` is also written by the same parser path, but it is written as `0` (`last=0501a294`), not lost later to a separate clear/reset.
- `selectedRoleIdShadow` is not repopulated afterwards; it remains `0` after the earlier clear.
- implication: the suspicious fields are now narrowed to parser/input semantics, not later local destruction. The current `1/1/4.actorinfo` payload is structurally accepted, but at least one parsed role-record value (`record+0xB0`) is being sourced as `0`, and the workspace-side selected-role shadow is not restored by the role-list response path.

## 2026-06-06
- changed: added `trace_title_rolelist_reader_methods` inside `title_handle_role_list_response()` at the two narrowest points:
  - right after the local reader object is initialized
  - again at the start of the first per-record parse iteration
- it logs the live stack parser buffer plus the five method pointers the decompiler exposes as `v20/v21/v22/v23/v24`.
- purpose: the next rerun should let us map the final `record+0xB0` writer (`v22`) back to its concrete reader function, so we can stop guessing whether that field is really “level” or some different trailing role attribute.
- validation: rebuilt with `make` after adding the watcher; build passed.

## 2026-06-06
- corrected: the first attempt at `trace_title_rolelist_reader_methods` used the wrong relocated hook addresses (`0x0501AA0E/0x0501AA2A`), so the single sampled `v20..v24` tuple from that run should be treated as invalid for `title_handle_role_list_response()`.
- fixed: the watcher points are now corrected to the real relocated `sub_3544()` offsets:
  - `0x0501A20E` (`static 0x363E`) for reader-methods-ready
  - `0x0501A22A` (`static 0x365A`) for first-record parse
- validation: rebuilt with `make` after fixing the relocation addresses; build passed.

## 2026-06-06
- verified: the corrected reader-method trace now resolves the `title_handle_role_list_response()` record parser tuple:
  - `v21 = 01033A5D -> stream_read_i32_be_tagged`
  - `v23 = 01033AAD -> stream_read_i8_tagged`
  - `v24 = 01033A1F -> stream_peek_i16_be`
  - `v20 = 01033A87 -> stream_read_cstr_len16`
  - `v22 = 01033A3B -> stream_read_i16_be_tagged`
- implication: the final per-record field written to `record+0xB0` is not a tagged `u32`; it is sourced from a tagged big-endian 16-bit value. This explains why the earlier synthetic builder, which still encoded the trailing field with `vm_net_mock_seq_put_u32()`, repopulated `role0.id/sex/job/name` correctly but always wrote `role0.level=0`.
- changed: updated the title-side `1/1/4.actorinfo` builder so the final trailing field in each compact role entry is now emitted with `vm_net_mock_seq_put_i16()` instead of `vm_net_mock_seq_put_u32()`.
- validation: rebuilt with `make` after the builder fix; build passed.

## 2026-06-06
- verified on the next rerun: the title-side compact role-entry fix works. `trace_title_role_workspace_write` now shows `role0.level -> 1` at the same parser write site (`last=0501a294`), and both `target50_init` and `target50_render` now sample `role0={id:10001 lvl:1 sex:0 job:1 name:侠剑江湖}`.
- corrected interpretation: `selectedRoleIdShadow` staying `0` at first render is no longer strong evidence of a parser defect. Static `role_manage_screen_select_role_slot()` shows that field is only written when the user confirms a normal role slot (`listPtr + 2044 = selected record id`) before the subsequent live `1/6/1` request.
- changed: added `trace_title_role_select_action` around the three narrowest local confirm-chain points:
  - `role_manage_enter_action_menu`
  - `role_manage_select_role_slot`
  - `role_manage_submit_selected_role`
- purpose: the next rerun should let us see whether a real role confirm writes `selectedRoleIdShadow`, switches local mode/family, and emits the live `1/6/1` request as expected.
- validation: rebuilt with `make` after adding the confirm-chain watcher; build passed.

## 2026-06-06
- verified: the current server-list crash at `pc=0x0001fff0` / `lastPc=0x0501b794` is caused by the compact title-side role-list record encoding `sex=0`. Static recovery of relocated `target50_render()` shows it computes a portrait/render-table index as `job * 2 + (sex - 1)` before the final indirect call at `0x0501B794..0x0501B797`; with `sex=0`, that subtraction wraps to `0xFF` and drives the BLX target to the invalid pointer `0x0001FFF0`.
- evidence: runtime crash report (`pc=1fff0`, `lr=501b797`, `lastPc=501b794`) matches the recovered `target50_render()` table-call site, and the same reruns still sampled `role0={id:10001 lvl:1 sex:0 job:1 name:侠剑江湖}` immediately before the crash.
- changed: tightened the title-side compact role-list builder in `src/main.c:vm_net_mock_build_title_role_list_actorinfo()` so the `sex` field is now clamped to the 1-based range `1..2` instead of the previous `0..1`.
- implication: this crash is not caused by `selectedRoleIdShadow`, by the outer `1/1/1 -> 1/1/16 -> 1/1/4` packet family, or by an empty role workspace. It is a narrower semantic mismatch inside the compact title role-entry contract used by `target50_render()`.

## 2026-06-06
- changed: added `tools/unpack_cbe_resources.py` to unpack the embedded `DF_DataPackage` from a `.CBE` and emit both wrapped loose-resource files plus decoded sidecars for the currently understood wrapper types.
- verified: `江湖OL.CBE`'s embedded resource area is a `LoadFormTCardEx`-style package rooted at `DF_Data_Pacakage_Offset`; the current main CBE carries `2` package blocks and `16` resources total.
- verified: package root `0` currently exports `game.mid` and `title.mid`, while child package `"1"` exports startup/UI assets plus the four opaque `MMORPG_Resource_Data_*.mid` payloads used by the update/install flow.
- evidence: structure recovered from `src/cbeParser.c` and `src/vmFunc.c` (`vm_DF_DataPackage_LoadFormTCardEx`, `vm_DF_DataPackage_GetFileByID`, `vm_GetStreamDataFormRes`), then validated by running the new parser over `bin/CBE/江湖OL.CBE`.

## 2026-06-06
- changed: added `tools/fix_jhonline_gif.py` to batch-convert `bin/JHOnlineData/*.gif` from the game's custom type-1 wrapper into standard directly-openable GIF files under `tmp/JHOnlineData_fixed_gif`.
- validation: full batch run matched `332` `.gif` files and produced `331` real conversions plus `1` placeholder GIF, with `0` hard failures.
- observed: one loose file currently named `加强.gif` is not a normal type-1 image resource; its bytes start with repeated `0xFE` interval markers and fail the usual `len_le32 + type` wrapper check, so the batch fixer now marks it as placeholder output instead of pretending to have recovered a real image.

## 2026-06-06
- changed: added `tools/batch_export_actor_gif.py` to batch-export `bin/JHOnlineData/*.actor` through the existing ordinary-actor GIF renderer, with one output directory per actor under `tmp/all_actor_gif`.
- changed: corrected ordinary actor image-name decoding in `tools/inspect_actor.py` from ASCII fallback to GBK, which fixes the effect-family actors that reference Chinese-named source GIFs such as `加强.gif`.
- validation: full batch run matched `132` `.actor` files and produced `129` real actor exports plus `3` placeholder outputs, with `0` hard failures and `887` total GIF files in `mode=both`.
- observed: the remaining placeholders are two synthetic `type=0xF1` mock wrappers (`c_mock_missing_motion.actor`, `mock_missing_motion.actor`) plus `f_buff.actor`, whose referenced source image is the abnormal loose file `加强.gif` already flagged by the GIF batch fixer as not being a standard type-1 image resource.

## 2026-06-06
- changed: added `tools/inspect_xse.py` to decode ordinary `type=2` `.xse` resources and heuristically recover their `XSE0` header, GBK text pool, function table, and command-name table.
- verified: all `149` loose `JHOnlineData/*.xse` samples currently decode as `type=2` resources whose payload starts with `XSE0`, then a bytecode/metadata region, then a `u32le + GBK` text pool, then a function table, then a length-prefixed ASCII command table.
- verified: every sampled `.xse` exports a `_MAIN` entry and `SHOWDIALOG`; `132/149` also expose `INIT` and `DOTASK`, strongly indicating that `.xse` is the game's scene/NPC quest-dialog script format rather than a passive text blob.
- validation: a full batch sweep over all loose `.xse` files succeeded with `0` parse failures. The most common command sets are task/dialog oriented: `INITTASK`, `GETCURRENTTASKID`, `GETTASKSTATE`, `ADDTASK`, `CHECKTASK`, `SETTASKSTATE`, `REMOVETASK`, `ADDTASKOPTION`, `TOCONTINUE`, and `SHOWDIALOG`.

## 2026-06-06
- verified: the local `.xse` command-call node is `0x1E 00 01 07 <u32 index>`, where `index` is zero-based into the file-local command table. Evidence: the one-command sample `04临安内城守卫.xse` reduces to `push string[0] -> call command[0]=SHOWDIALOG -> return`, and larger quest scripts show call operands `0..9` matching their exported command arrays.
- verified: the simplest stable operand-load node is `0x1A 00 01 <mode> <u32 value>`. Current high-confidence operand modes are `mode=0` integer literal, `mode=2` text-pool string reference, `mode=3` local variable/state-slot reference, and `mode=8` temporary/last-result slot reference.
- high-confidence hypothesis: `.xse` task/dialog control flow is built around additional nodes `0x13`, `0x14`, `0x1B`, and `0x1D`. The current evidence points to `0x13/0x14` as control-flow targets/jumps and `0x1B/0x1D` as comparison/branch-structure helpers used around `GETCURRENTTASKID` / `GETTASKSTATE` result handling, but their exact packed field layout still needs one more round of interpreter-side RE.

## 2026-06-06
- verified: the real local confirm chain on the recovered title role-list path is now working. On the latest rerun, `role_manage_screen_select_role_slot()` wrote `selectedRoleIdShadow = 10001`, switched local `mode` to `3`, and immediately emitted a live outbound WT packet `hdr=1/6 objs=1/1/6` with payload field `actorID=10001`.
- evidence: `bin/logs/net_trace.log` lines for `trace_title_role_select_action label=role_manage_select_role_slot`, `trace_title_role_workspace_write label=selectedRoleIdShadow ... new=00002711`, `trace_title_login_write label=mode ... new=00000003`, and trailing `unhandled_packet_payload len=25 hex=575400190101060015076163746f7249440006000400002711`; matching wire summary in `bin/logs/net_packets.log`.
- changed: added a minimal built-in handler for that live role-select request. The mock now answers top-level `1/6` / object `1/1/6` with a single `1/1/6` response object carrying `result=1` and echoed `actorID`, logged as `builtin-title-role-select`.
- rationale: static `role_manage_screen_handle_network(case 6)` does not currently read any response fields before persisting `mmorpg_LoginRecord`, so this first validation keeps the contract intentionally minimal instead of guessing richer scene-enter payloads too early.
- validation: rebuilt with `make` after adding the new `1/1/6 actorID` response path; build passed.

## 2026-06-06
- verified on the next rerun: the new live role-select ack is accepted by the real role-manage network path. After confirming the first role slot, the client again writes `selectedRoleIdShadow = 10001`, switches to `mode=3`, receives `builtin-title-role-select`, and then forwards the resulting `packetSubtype=6` into `target50_handle_network`.
- evidence: `bin/logs/net_packets.log` now shows `source=builtin-title-role-select responseLen=39 WT len=25 hdr=1/6 objs=1/1/6 count=1`, with payload `result=1` and echoed `actorID=10001`; matching `bin/logs/net_trace.log` lines `trace_title_child_manager_call ... child=0501f850 ... packetKind=1 packetSubtype=6` and `trace_title_role_path label=target50_handle_network ... mode=3 ... selectedRoleIdShadow=10001`.
- corrected implication: the live `1/1/6 actorID` contract is no longer missing at the transport level. A lone `1/1/6` ack is sufficient to enter `role_manage_screen_handle_network(case 6)`, but it does not by itself trigger any visible local transition or later scene/bootstrap requests on this rerun.
- next: stop treating “no handler for 1/6” as the blocker. The narrower question is now whether the real server replies with additional objects after subtype `6` (title-side or scene-side), because the current single-object ack appears too weak to move the client beyond `mode=3`.

## 2026-06-06
- changed: tightened the next role-select experiment to a minimal two-object title-local combo. `builtin-title-role-select` now answers live role confirm with `1/1/6 { result=1, actorID=<echo> }` immediately followed by the existing subtype-15 success shell, i.e. `1/1/6 + 1/1/15`.
- rationale: static `role_manage_screen_handle_network()` shows `case 6` alone only persists `mmorpg_LoginRecord`, while `case 15(result=1)` is the nearest sibling branch that causes an immediate local mode transition (`role_manage_screen_enter_action_menu()`).
- validation: rebuilt with `make` after changing the role-select response combo; build passed.

## 2026-06-06
- corrected: the new `1/1/6 + 1/1/15` role-select combo exposed a different crash that is not caused by the title-side compact role record anymore. The failing PC is `0x01033A68` with `lr=0x0100FAC5`, which maps back to `stream_read_i32_be_tagged_impl()` under `parse_actorinfo_response()`.
- verified: static `scene_runtime_init_and_sync()` explains the crash path. Once the role-select follow-up advances the client, the main scene-side bootstrap also consumes the same subtype-6 object through its cached packet loop. For `type=1` packets with `subtype==6`, it reads `revivetype/ruffianflag/type/practiseflag/pcimg/expcard/expbook/practiseinfo/...`, and if `pcimg==1` it allocates a blob and calls the scene-object parser slot `+0x3C`, i.e. `parse_actorinfo_response()`.
- implication: the old minimal `1/1/6 {result, actorID}` ack is no longer safe once subtype-15 is appended. The crash is strong evidence that the scene side treats this reply as a full subtype-6 scene-enter family packet, not as a tiny title-local ack.
- changed: updated `builtin-title-role-select` so its first object now reuses the existing full subtype-6 success shell (`revivetype/ruffianflag/type/practiseflag/pcimg/expcard/expbook/practiseinfo/lastexp/curexp/persentexp/actorinfo`) before appending `1/1/15 {result=1}`. This keeps the subtype-15 local-mode experiment intact while removing the known null-blob hazard from the scene-side subtype-6 consumer.
- validation: rebuilt with `make` after replacing the minimal role-select subtype-6 ack with the full scene-compatible subtype-6 shell; build passed.
- changed: added a narrow scene-runtime compatibility shim in `src/main.c` for the first-scene HUD divide-by-zero. At `scene_rebuild_status_meter_node()` seed gate (`0x0100FF26`), when runtime is still in `n2==1`, `R9+0x6048` is empty, and both meter display maxima are still `0/0`, the emulator now seeds `sceneStatusMeterNode + 0xC4/+0xC8` and the mirrored current-node `+0xC4/+0xC8` from the already-recovered base maxima. The new trace label is `trace_status_meter_seed_fallback`.
- evidence: latest `bin/logs/net_trace.log` crashing session shows `trace_status_meter_rebuild_site label=seed_gate ... n2=1 sourceHead=00000000 currentBase=120/100 meterDisplay=0/0`, then first HUD draw faults at `trace_status_bar_divide_site label=primary ... baseMax=120 displayMax=0`; `scene_rebuild_status_meter_node()` static branch still only performs the built-in seed when `n2==2`, so this shim is intentionally mirroring the missing fallback on the confirmed empty-head path rather than masking an arbitrary divide.
- next: rerun and confirm whether `trace_status_meter_seed_fallback` appears before the first `scene_draw_status_panels()` pass, whether the `pc=0x0101477E` divide-by-zero disappears, and what the next post-scene/bootstrap blocker becomes once HUD display maxima are non-zero on first draw.

## 2026-06-06
- verified on the next rerun: the narrow HUD fallback removed the old post-scene packet assert from the active path. `bin/logs/net_packets.log` now shows `source=builtin-scene-resource-followup` for the earlier unhandled `WT len=49` / `Type=101` request, so the client proceeds into the later resource-update flow instead of crashing there.
- verified: the new visible blocker is a real `18/7` update loop on the resource-update screen. `bin/logs/net_trace.log` shows repeated `trace_update_request_prepare ... name=侠剑江湖 type=1 start=0/4096/...`, and the same session visibly sits on `"正在更新资源文件, 请稍候!"`.
- verified: the previous `18/7` mock reply was still writing the wrong local key. Even when the request name is `侠剑江湖`, the old trace logged `mock_update_chunk_response name=MMORPGTempcbm ...`, and `bin/logs/storage_trace.log` shows the temp file being renamed to `JHOnlineData/MMORPGTempcbm` before the client immediately fails another local open for `JHOnlineData/侠剑江湖`.
- changed: tightened `vm_net_mock_build_update_chunk_response()` / `vm_net_mock_load_resource_update_payload()` so the built-in `18/7` reply now:
  - extracts request fields `name` and `type`
  - echoes the same `name/type` back instead of hardcoding `MMORPGTempcbm` / `0`
  - tries an exact local payload lookup at `JHOnlineData/<request.name>` before falling back to the old generic update payload sources
- rationale: this is a narrow compatibility fix aimed at the confirmed rename/reopen mismatch. It does not assume the true semantic format of the misrouted `侠剑江湖` resource is already understood.
- validation: rebuilt with `make` after the `18/7` echo fix; build passed. Runtime confirmation of the new rename/reopen behavior is pending the next manual rerun.

## 2026-06-06
- verified on the next rerun: the first `18/7` resource-update phase now preserves the requested packet key on the wire. `bin/logs/net_packets.log` shows response `type=1` with `name` bytes `cfc0bda3bdadbafe` (the same non-ASCII key the request sent), and `bin/logs/net_trace.log` now logs `mock_update_chunk_response reqName=... reqType=1 name=... type=1`.
- corrected implication: the active blocker is no longer the first `18/7` response contract itself. The same run still fails afterwards because the emulator's local file shim decodes the guest path bytes inconsistently: `bin/logs/storage_trace.log` shows the temp file renamed from `JHOnlineData/MMORPGTempbin` to a garbled host filename (`bin/JHOnlineData/燨QR_lVn`) instead of the intended selected-role resource key, followed immediately by another reopen failure for the same resource.
- changed: tightened `src/vmFunc.c::vm_read_path_string()` so non-ASCII guest path strings now keep ASCII/valid UTF-8 as-is, but when the byte stream is not valid UTF-8 it is converted from guest GBK into host UTF-8 before the later open/rename normalization path runs.
- rationale: this keeps the fix in the emulator filesystem bridge, where the mismatch is now evidenced, rather than adding more protocol-side special cases after the first `18/7` request is already correct on the wire.
- validation: `make` recompiled the changed object successfully, but replacing `bin/main.exe` was blocked because the running emulator process kept the file open. A full alternate link to `bin/main_patched.exe` succeeded, so the code change itself is build-clean; runtime verification is pending the next rerun on the new binary.

## 2026-06-06
- corrected: the previous filesystem-bridge tweak was too broad and caused a new early startup assert. On the next run, `bin/logs/stdout_trace.log` reports `vMAssert file=..\\..\\code\\src\\DF_PictureLibrary.c line=95 lr=01000647`, and `bin/logs/storage_trace.log` shows the immediate cause: startup can no longer open `CBE/江湖OL.CBE` after `vm_read_path_string()` rewrites the original narrow GBK path into UTF-8 bytes that the current host `fopen` path does not resolve.
- verified: the failing rename/open case and the startup CBE case use two different guest path encodings:
  - startup `CBE/江湖OL.CBE` arrives as a normal narrow GBK byte string (`43 42 45 2F BD AD BA FE 4F 4C ...`)
  - the later resource-rename target arrives as UCS2-LE (`A0 4F 51 52 5F 6C 56 6E 00 00`, i.e. `侠剑江湖`)
- changed: replaced the broad UTF-8 fallback in `src/vmFunc.c::vm_read_path_string()` with a narrower path decoder:
  - detect likely UCS2-LE path strings by validating 16-bit code units
  - convert only those UCS2 paths through `ucs2_to_gbk()`
  - leave ordinary narrow/GBK path strings untouched
- rationale: this matches the two confirmed on-wire/on-memory path families without changing the host file API expectations for existing GBK paths.
- validation: rebuilt with `make` after the decoder rollback+UCS2 fix; build passed.

## 2026-06-06
- verified on the next rerun: the narrowed path decoder is now good enough for the first resource-update reopen path. `bin/logs/storage_trace.log` shows `MMORPGTempbin -> JHOnlineData/�������� success=1`, followed immediately by a successful reopen of the same path and `file_read ... len=4 hex=fefefefe`.
- corrected implication: the active blocker is no longer the `18/7` packet key echo or the file rename/open bridge. Static IDA for `sub_100D338()` (`0x0100D338`) shows that this local resource-open path first reads a little-endian 4-byte payload length, allocates that many bytes, and only then reads the body. The current fallback `18/7(type=1,name=侠剑江湖)` still downloads raw `mmGameMstarWqvga.cbm` bytes, so the reopened file begins with `fefefefe` and is interpreted as an invalid giant length rather than as a normal local resource stream.
- changed: tightened `src/main.c::vm_net_mock_load_resource_update_payload()` again on the narrow named-fallback path. When `18/7` carries a non-empty `name`, there is no exact local `JHOnlineData/<name>` file, and the request is still on the named resource branch, the mock now wraps the generic fallback payload as `u32le payloadLen + payload` and logs `mock_update_named_resource_wrapper`.
- rationale: this matches the confirmed `sub_100D338()` consumer contract while leaving the already-working exact-name lookup path and the earlier synthetic `0xF1` actor/image resource wrappers untouched.
- validation: rebuilt with `make` after the named-resource wrapper change; build passed. Runtime confirmation is pending the next manual rerun.

## 2026-06-06
- corrected on the next rerun: the new named-resource wrapper was not the immediate crash site yet. The latest session never reaches a fresh `18/7` request at all; instead, right after first scene `actor_pass`, the client directly reopens the already-downloaded local key `JHOnlineData/侠剑江湖`, and that stale on-disk file still begins with `fefefefe`.
- evidence: newest `bin/logs/storage_trace.log` tail shows `file_open handle=3 path=JHOnlineData/�������� mode=rb` followed immediately by `file_read ... len=4 hex=fefefefe`, while the same newest `bin/logs/net_packets.log` session ends before any new `source=builtin-update-chunk` for this run. Directory listing confirms both stale files now exist on disk: `JHOnlineData/侠剑江湖` and the older garbled sibling `JHOnlineData/燨QR_lVn`, each `48858` bytes.
- implication: there are now two separate cache hazards to handle:
  - the client must not treat an existing extensionless Chinese-named local file with an impossible `u32le` header as a valid resource stream
  - the mock server must not treat that same stale file as an exact local payload source for the next named `18/7` response
- changed:
  - `src/vmFunc.c::vm_get_file_handle()` now rejects read-only opens of `JHOnlineData/<non-ascii name>` files with no extension when the first 4 bytes are not a plausible `u32le payloadLen <= fileSize-4`, logging `file_open_reject_invalid_named_cache`
  - `src/main.c::vm_net_mock_load_resource_update_payload()` now also ignores such stale exact-name payloads on the server side, logging `mock_update_named_resource_reject_invalid_cache`, and then falls through to the new wrapped fallback builder instead of re-serving the same bad bytes
- rationale: this keeps the fix extremely narrow. It only targets the confirmed bad-cache family (extensionless, non-ASCII, read-only local resource key) and leaves normal `.actor/.map/.sce/.gif/.dsh` loads plus startup `MMORPGTempcbm` behavior unchanged.
- validation: rebuilt with `make` after the invalid-cache guards; build passed.

## 2026-06-07
- verified: the scene HUD divide-by-zero is no longer the active blocker. Latest `bin/logs/net_trace.log` reaches `tick=291` with `trace_status_bar_divide_site label=primary ... baseMax=120 displayMax=120` and `label=secondary ... baseMax=100 displayMax=100`, then continues into `scene_runtime_tick label=actor_pass`.
- verified: the wrapped named-cache path is also no longer failing at the old `fefefefe` header check. Latest `bin/logs/storage_trace.log` reopens `JHOnlineData/侠剑江湖`, reads `hex=dabe0000`, then reads the `48858`-byte wrapped body successfully.
- corrected implication: the current flash-exit is now later and more specific than “named update body still has no length header”. At `tick=289`, `scene_hud_main_panel_init()` forwards a non-ASCII string through `parse_actor_motion_descriptor()` into `load_resource_stream_by_name()` (`trace_sub_10352ae_call_from_scene_rebuild`, `trace_parse_actor_motion_entry`, `trace_resource_stream_call_from_actor_motion`), and the same session later reopens `JHOnlineData/侠剑江湖`. This makes the stronger current hypothesis “scene actorinfo mid-body string is being mis-filled as role display name” rather than “the trailing scene-key field is still wrong”.
- evidence:
  - `bin/logs/net_trace.log` lines around the newest `tick=289..291` window:
    - `trace_sub_10352ae_call_from_scene_rebuild ... stack0Name=<nonascii:01056a16>`
    - `trace_parse_actor_motion_entry ... arg4Name=<nonascii:01056a16>`
    - `trace_resource_stream_call_from_actor_motion ... namePtr=01056a16`
    - later `trace_resource_open_helper ... namePtr=0540010a name=<nonascii:0540010a>`
  - `bin/logs/storage_trace.log` tail for the same session:
    - `file_open handle=3 path=JHOnlineData/�������� mode=rb`
    - `file_read ... len=4 hex=dabe0000`
    - `file_read ... len=48858 ... ascii=...MMORPGInGame...`
  - static IDA:
    - `scene_rebuild_runtime_nodes()` (`0x0100F7A6`) passes `sceneCurrentNode + 0x60` into `scene_hud_main_panel_init()` as `currentActorName`
    - `parse_actor_motion_descriptor()` (`0x0100D6E2`) immediately feeds its incoming resource-name argument into `load_resource_stream_by_name()`
- changed: tightened `src/main.c:vm_net_mock_build_actor_info()` so the earlier 20-byte mid-body string now uses `vm_net_mock_actor_motion_resource_name(actorJob, actorSex)` instead of the role display name `侠剑江湖`. Added `motion=%s` to `mock_login_actor_response` trace output to make the next rerun easier to confirm.
- rationale: the current runtime now proves that this field is consumed as a local resource identifier on the live scene path. Reusing the role display name there forces the client down the wrong `JHOnlineData/侠剑江湖` asset chain even after the update-cache wrapper issues are fixed.
- validation: rebuilt with `make` after the actorinfo mid-body resource-name fix; build passed.

## 2026-06-07
- corrected: the previous “mid-body string should be `.actor`” conclusion was too coarse. Static IDA for `parse_actorinfo_response()` (`0x0100FA88`) now pins the earlier two-string block to `actor+0x100` (10 bytes) and `actor+0x10A` (20 bytes), while a later separate string still lands at `actor+0xD8`.
- verified: the latest late crash/open matches that exact `actor+0x10A` slot. `bin/logs/net_trace.log` ends the failing actor pass with `trace_resource_open_helper ... namePtr=0540010A name=h_warrior.actor`, which is the same offset the parser uses for the 20-byte mid-body string.
- verified: the normal `.actor` container path is still healthy and distinct. The same session earlier shows `trace_resource_stream_call_from_db82 ... name=h_warrior.actor`, followed by direct image opens `h_warriorwalk1.gif`, `h_warriorwalk2.gif`, `jian.gif`, `guang1.gif`, `guang2.gif`, `death.gif`, and `ying.gif`.
- implication: the active mismatch is now narrower and better grounded. The `actor+0x10A` mid-body field is feeding a direct image-decode consumer, so it must carry a concrete image name, while the later `actor+0xD8` field remains the real `.actor` resource string for the descriptor/parser path.
- changed: updated `src/main.c:vm_net_mock_build_actor_info()` again so the 20-byte mid-body field now uses a new direct preview-image helper (`h_warriorwalk1.gif`/`hW_warriorwalk1.gif`/`h_assassinwalk1.gif`/`hW_assassinwalk1.gif`/`h_magewalk1.gif`/`hW_mage1.gif`) instead of the earlier `.actor` fallback. The trailing `.actor` field is unchanged. `mock_login_actor_response` now logs `preview=%s actor=%s`.
- validation: rebuilt with `make` after the `actor+0x10A -> first gif` correction; build passed.

## 2026-06-07
- verified: the new actor preview-image correction is good enough to keep the client alive in-scene. Latest logs no longer show a crash/assert after scene entry; the client reaches the map HUD and keeps ticking through later `scene_runtime_tick` passes.
- corrected: the next active gap is back in the old `WT len=49` scene follow-up family, but now as a response-shape mismatch rather than an unhandled assert. `bin/logs/net_packets.log` shows the request still carries seven objects: `1/12/1, 1/7/42, 1/6/1, 1/6/13, 1/6/14, 1/2/10, 1/25/5`, while the previous built-in reply only emitted five objects and substituted `1/25/12` for `1/25/5`.
- verified: static `net_handle_task_response_dispatch()` (`0x0104726C`) now recovers two more safe minimal contracts:
  - case `13` (`tasktypes`) walks exactly six entries, each parsed as `(i16 + short string)`
  - case `14` first reads `action`; when `action==0`, it takes the zero-item path `tasknum + taskinfo`
- changed: widened `builtin-scene-resource-followup` in `src/main.c` from the earlier 5-object shell to a full 7-object compatibility reply:
  - `1/12/1 {learnednum=0, learnedskill=\"\"}`
  - `1/7/42 {booknum=0, booksinfo=\"\"}`
  - `1/6/1 {taskinfo=<empty blob>}`
  - `1/6/13 {tasktypes=<6 x (0, \"\")>}`
  - `1/6/14 {action=0, tasknum=0, taskinfo=<empty blob>}`
  - `1/2/10 {othernum=0}`
  - `1/25/5 {result=4}`
- rationale: this keeps every newly added object on a statically confirmed zero-item branch, but removes the obvious family/count mismatch from the previous reply.
- validation: rebuilt with `make` after the fuller 7-object scene-followup reply; build passed.

## 2026-06-07
- verified on the next manual rerun: the fuller `builtin-scene-resource-followup` reply is accepted on the live scene path. `bin/logs/net_packets.log` now shows `source=builtin-scene-resource-followup responseLen=246` for the former `WT len=49` site, and `bin/logs/net_trace.log` logs `mock_scene_resource_followup_response objects=7 ... len=246` followed by `handled_packet ... count=7`.
- verified: this is no longer just “packet handled without immediate assert”. After the queued `event=7` is dispatched, `runtime_state` drops `loadFlags` from `1,0,0` to `0,0,0`, and the same log tail continues through repeated `trace_scene_runtime_tick label=draw_pass`, `status_panels`, and `actor_pass`.
- verified: the older first-frame HUD crash is still gone on this run. `bin/logs/net_trace.log` reaches `trace_status_bar_divide_site` with `displayMax=120/100`, and later samples at `tick=757` and `tick=857` show the primary meter rising `current=1 -> 2` while `displayMax` stays nonzero.
- verified: there is no newer wire-level blocker in this session tail. No additional `send_payload`, no new unhandled-packet marker, and no assert/abort appear after the accepted 7-object follow-up; `bin/logs/stdout_trace.log` only contains normal `ScreenResume/ScreenInit` lines.
- implication: the old “scene first-frame follow-up packet is still the active blocker” hypothesis is now retired. The next real missing contract is more likely to surface only after a later manual in-scene action triggers new client behavior.

## 2026-06-07
- verified on the next in-scene interaction run: the visible center/bottom progress strip is not currently backed by a new server wait. After the accepted `builtin-scene-resource-followup` packet, `bin/logs/net_packets.log` contains no newer `send_payload`; the later session tail only adds local `mouse_event` and `touch_dispatch entry=050823bf screen=01053438` lines.
- verified: the same newest runtime state stays fully past the old network gates while that strip remains visible. `bin/logs/net_trace.log` still shows `sceneTickGate=1,1` and `loadFlags=0,0,0` during repeated `trace_scene_runtime_tick label=draw_pass/status_panels/actor_pass`, with no accompanying unhandled packet or assert.
- verified by static IDA: `scene_runtime_tick()` (`0x01014D3A`) only enters the direct loading-strip helper `sub_1003568() -> sub_100337C(0)` when either scene gate byte at `R9+0x5C67/+0x5C68` is zero. That does not match the newest live tick state, so the currently visible strip should not be treated as proof that the runtime is still waiting on another immediate WT response.
- implication: the active blocker has shifted from protocol back into local scene/UI state. The next useful reverse-engineering target is the draw owner for that same strip region under normal scene repaint, or the local state that should clear/replace the earlier one-shot strip after scene gates are already `1,1`.

## 2026-06-07
- changed: added a narrow local draw-owner trace for the persistent scene strip instead of broad frame logging. `src/main.c` now logs `trace_progress_strip_shape` when a `*Rect*` LCD draw overlaps screen region `x=16..224, y=200..286`, and `src/vmFunc.c` now logs `trace_progress_strip_draw` when `vMDrawImageWithClipEx` or `vMDrawImageClipAndAlphaEx` blits into that same band.
- rationale: latest packet/runtime evidence already rules out a new immediate server wait (`sceneTickGate=1,1`, `loadFlags=0,0,0`, no newer `send_payload` after manual touch), so the highest-value next step is to identify which local draw path is repainting the moving strip under normal scene ticks.
- scope: the new trace is read-only, region-filtered, and count-limited (`48` shape hits / `64` image hits) to keep `bin/logs/net_trace.log` usable during manual reruns.
- validation: rebuilt with `make` after adding the progress-strip draw trace; build passed.

## 2026-06-07
- corrected on the next rerun: the first `trace_progress_strip_draw` hits were not the late scene strip itself. `bin/logs/net_trace.log` shows all 64 image hits at `tick=2`, immediately after `trace_loading_overlay_call label=sub_100337C`, and every hit shares `last=0100413e lr=01004141`.
- verified by static IDA: `0x0100413E` is the inner `BLX` inside `sub_1004096()`, a clipping wrapper used by LCD manager `reserved2`, and `sub_100337C()` uses that path to tile the startup/loading overlay bar from a `36x36` source image across an off-screen `240x400` buffer.
- implication: the first region filter was too broad and mostly captured the startup overlay's tiled background, not the later persistent in-scene strip. This does still confirm one useful thing: the old overlay bar is image-based and routed through `sub_100337C()` very early.
- changed: tightened the trace again so future `trace_progress_strip_shape` / `trace_progress_strip_draw` hits only fire when the scene is already settled (`sceneTickGate=1,1`, `loadFlags=0,0,0`), and image hits now also skip oversized off-screen targets such as the `240x400` map/loading buffer.
- validation: rebuilt with `make` after the settled-scene/off-screen filtering change; build passed.

## 2026-06-07
- corrected again on the next rerun: `240x400` is the real LCD size, not an oversized off-screen target. The newest `trace_progress_strip_draw` hits therefore matter, but they still do not belong to steady `scene_runtime_tick` repaint.
- verified: the visible loading-strip helper is re-entered from the live scene bootstrap itself. `bin/logs/net_trace.log` now shows `trace_loading_overlay_call label=sub_1003568` / `sub_100337C` at `tick=182` with `activeScreen=01053450 currentThis=01053438 sceneTickGate=1,1`, and the first tiled strip draw batch follows immediately with the same `last=0100413e/lr=01004141`.
- verified by static IDA: both `lr=0100f827` and `lr=0100f89d` land inside `scene_rebuild_runtime_nodes()`. So the old loading overlay is being redrawn during scene runtime-node / HUD bootstrap, not only during the early startup/title path.
- verified: after the accepted `builtin-scene-resource-followup`, steady scene ticks resume at `tick=185+` with `loadFlags=0,0,0`, but there are no newer `trace_progress_strip_draw` or `trace_progress_strip_shape` hits in that settled tail.
- implication: the current persistent strip now looks like a stale bootstrap/loading overlay that scene bootstrap redraws once and later steady scene repaint fails to clear or overwrite, rather than an actively animated wait bar driven by post-followup network traffic.

## 2026-06-07
- verified by deeper static pass: the scene bootstrap path redraws that overlay from more than just `scene_rebuild_runtime_nodes()`. `sub_1003568()` xrefs and `scene_runtime_init_and_sync()` disassembly now show a whole bootstrap ladder of redraws at `0x01013598`, `0x01013666`, `0x010136B8`, `0x01013772`, `0x010137AC`, `0x010137BA`, `0x010137E0`, and `0x010137F2`, in addition to the two `scene_rebuild_runtime_nodes()` sites `0x0100F822` and `0x0100F898`.
- verified against runtime: the live rerun hits exactly that ladder at `tick=182` with `sceneTickGate=1,1`, logging `trace_loading_overlay_call label=sub_1003568` from return PCs `0100f827`, `0100f89d`, `0101359d`, `0101366b`, `010136bd`, `01013777`, `010137b1`, `010137bf`, `010137e5`, and `010137f7`.
- implication: the current visual blocker is now narrow enough for a blocker-removal shim. The scene/bootstrap code itself keeps redrawing the old loading overlay after scene gates are already open, and later steady repaint never clears it.
- changed: added a narrow runtime suppression shim in `src/main.c` at `0x01003568` entry. When `sub_1003568()` is called with `sceneTickGate=1,1` and the return address matches one of the confirmed scene-bootstrap callers above, the emulator now logs `trace_loading_overlay_suppressed`, returns `0`, and skips the old overlay draw. Other startup/title/loading overlay uses of `sub_1003568()` are unchanged.
- rationale: this is narrower than a post-hoc screen-region wipe and grounded by both static xrefs and live callsite traces.
- validation: rebuilt with `make` after the scene-bootstrap overlay suppression shim; build passed.

## 2026-06-07
- corrected on the next rerun: the first suppression shim did not actually fire. The new session still logs the full `tick=182` `trace_loading_overlay_call label=sub_1003568` ladder, but there is no `trace_loading_overlay_suppressed` line at all.
- verified: the active visible strip is no longer explainable only as a stale bootstrap draw. Later in the same session, after a second accepted `builtin-scene-resource-followup` at `tick=305`, the settled scene tail (`tick=306+`, `loadFlags=0,0,0`) repeatedly emits new strip-region draws every frame.
- verified from runtime trace:
  - repeated `trace_progress_strip_draw ... api=vMDrawImageClipAndAlphaEx ... srcDim=96,24 ... dy=188 ... last=01004220`
  - repeated `trace_progress_strip_draw ... api=vMDrawImageWithClipEx ... srcDim=19,1` then `38,1 ... dx=33 dy=200..202 ... last=0100413e`
  - these hits appear immediately after `trace_scene_runtime_tick label=actor_pass` in the settled scene tail, not only during bootstrap
- implication: the persistent bar now has a confirmed steady-scene draw owner family, and the bootstrap overlay suppression hypothesis is insufficient by itself.
- changed: kept the shim in place for now, but extended diagnostics instead of widening behavior:
  - `trace_loading_overlay_candidate` now logs when a confirmed scene-bootstrap caller reaches `sub_1003568()`, so the next rerun can explain why suppression did not trigger
  - `trace_progress_strip_draw` now also logs the wrapper's saved caller return so the next rerun can attribute the steady `96x24` / `38x1` bar draws to the real higher-level scene/HUD function
- validation: rebuilt with `make` after the added candidate/caller tracing; build passed.

## 2026-06-07
- verified on the newest manual rerun: the scene-bootstrap suppression shim now does fire. `bin/logs/net_trace.log` contains `trace_loading_overlay_candidate` and matching `trace_loading_overlay_suppressed` lines for the confirmed ladder at `0100F826`, `0100F89C`, `0101359C`, `0101366A`, `010136BC`, `01013776`, `010137B0`, `010137BE`, `010137E4`, and `010137F6`.
- verified: despite that suppression, the settled scene still draws the visible strip family after `trace_scene_runtime_tick label=actor_pass` with `sceneTickGate=1,1` and `loadFlags=0,0,0`. The newest tail again shows repeated `96x24`, `32x107`, and `38x1` strip-region draws at `tick=746+`.
- verified by static IDA against the new trace: `caller=0100424B` is `sub_1004226()` and `caller=01004175` is `sub_1004150()`. Both are picture-library wrappers that first fetch the current screen image from `R9+0x4D6C` and then dispatch to the lower clip helpers `sub_1004178()` / `sub_1004096()`, so they are still not the real steady-scene owner.
- changed: added one narrower diagnostic layer in `src/main.c` that traces direct entry into `sub_1004150()` / `sub_1004226()` only when the scene is already settled and the requested destination overlaps the persistent strip band. The new log keyword is `trace_progress_strip_wrapper`.
- validation: rebuilt with `make` after the wrapper-entry trace addition; build passed.

## 2026-06-07
- verified on the next rerun: `trace_progress_strip_wrapper` resolves the steady-scene owner above the picture-library wrappers. The `96x24` icon row comes from `loading_gif_widget_draw_frame()` callsites `010460CA/010460F6/01046130/01046158`, and the accompanying `19x1/38x1` line pieces come from helpers `sub_10058E6()` / `sub_1005A0C()` / `sub_1005AC4()` inside that same widget draw family.
- verified by static IDA: `loading_gif_widget_init()` has only two construction sites in the binary:
  - `startup_screen_update_init()` builds the startup/update-screen copy
  - `scene_get_loading_gif_widget()` builds the scene-runtime copy rooted at `Global_R9 + 0x60F4`
- implication: the moving center/bottom strip remaining after scene entry is the scene-runtime `loading.gif` widget still drawing in steady scene, not a new packet wait or a generic unknown HUD bar.
- changed: added a very narrow scene-only suppression shim in `src/main.c` at `loading_gif_widget_draw_frame()` entry (`0x01046048`). When `R0 == Global_R9 + 0x60F4` and the scene is already settled (`sceneTickGate=1,1`, `loadFlags=0,0,0`), the emulator clears widget byte `+16`, logs `trace_scene_loading_gif_widget_suppressed`, returns `0`, and skips only the scene copy of the widget. The startup/update-screen widget path is untouched.
- validation: rebuilt with `make` after the scene-only `loading.gif` suppression shim; build passed.

## 2026-06-07
- verified on the latest rerun: the scene-only `loading.gif` suppression shim does fire exactly as intended. `bin/logs/net_trace.log` now shows repeated `trace_scene_loading_gif_widget_suppressed ... widget=01056CC4` after scene settle.
- verified: that shim removes only part of the old strip family. After the first suppression hit at `tick=477+`, the settled scene no longer emits the earlier `96x24` icon-row or `38x1` line-row calls from `loading_gif_widget_draw_frame()`, but it still emits a second residual draw family:
  - `sub_1005A0C()` -> `sub_1004150()` drawing `srcDim=240,293 dst=0,67`
  - `sub_10058E6()` -> `sub_1004226()` drawing `srcDim=32,107` slices at `dst=31..62,180..225`
- implication: the visible bar is composite. The `loading.gif` widget was one owner and is now suppressed; the remaining strip/background fragments come from a second local panel path that reuses picture-library helpers at return PCs `01005A68`, `01005948`, and potentially `01005B54`.
- changed: added a second, still narrow residual-strip suppression shim in `src/main.c`. At `sub_1004150()` / `sub_1004226()` entry, if the scene is settled, the destination overlaps the persistent strip band, and the direct caller is one of `0x01005948`, `0x01005A68`, or `0x01005B54`, the emulator logs `trace_scene_progress_strip_residual_suppressed`, returns `0`, and skips only that residual strip draw.
- validation: rebuilt with `make` after the residual strip suppression shim; build passed.

## 2026-06-07
- corrected by later full decompile: this earlier “starts with `u32 level`” summary was still wrong. The first post-display-name field is a tagged `u32 summaryStatus` that the parser later truncates into the word slot at `actor+0x138`; display-max fields also sit before `shortLabel/previewImage`, not after them.
- verified by adjacent static uses: the first visual byte is `sceneCurrentNode->visualVariant` and drives class growth in `scene_apply_levelup_status_growth()`, while the second is `visualGroup`; the previous mock was still writing user-facing `job=1..3` / `sex=0..1` directly, so the scene actorinfo stream was misaligned from the level field onward.
- changed: rewrote `vm_net_mock_build_actor_info()` to follow the confirmed scene parser order instead of the older rough guess. The builder now:
  - writes `visualVariant=job-1` and `visualGroup=sex`
  - places `roleLevel` into the first post-string `u32` slot
  - writes the two status bars as `{current, baseMax, current, baseMax}` before the later display-max pair
  - keeps the preview-image / actor-resource / sceneKey trailing strings in their parser-confirmed slots
- evidence: latest runtime traces before this fix showed `role0.level` already corrected in title data, but scene still displayed level `0`; static decompile now pins that mismatch on the large scene actorinfo order rather than on title role-list data.
- verified from runtime trace: a later manual scene interaction still emitted a standalone `WT len=19 hdr=2/10 objs=1/2/10` and previously hit `assert(0)` because the mock only handled `1/2/10` when bundled inside the `len=49` follow-up family.
- changed: added a narrow standalone `builtin-actor-other-only10` reply that returns the same minimal `1/2/10 {othernum=0}` object for the one-object request shape, without changing the existing bundled follow-up response.
- validation: rebuilt with `make` after the actorinfo reorder and standalone `1/2/10` handler; build passed.

## 2026-06-07
- verified by firmware static analysis in `8533n_7835.axf`: `CdRectPoint` lives at `0x67C37E` and implements the inclusive predicate `right >= x && left <= x && bottom >= y && top <= y`.
- changed: replaced the old `assert(0)` stubs for `CdRectPoint` with a real emulator implementation in both hook tables:
  - `hook_vm_manager_game_util_func()` slot `idx=2`
  - `hook_vm_manager_gameold_code_callback()` slot `idx=51`
- implementation detail: the hook now reads `(left, top, right, bottom)` from `R0..R3`, reads `(x, y)` from the stack, and returns `0/1` exactly like the firmware helper.
- validation: rebuilt with `make` after the `CdRectPoint` implementation; build passed.

## 2026-06-07
- corrected again by full decompile of `parse_actorinfo_response()` (`0x0100FA88`): the previous “scene actorinfo after second string starts with `u32 level`” summary was still one field family too coarse. On the `a2==0` scene path, the confirmed order after the second string is:
  - tagged `u32 summaryStatus`, truncated into `actor+0x138`
  - `u32 primaryCurrent(+0xB4)`
  - `u32 primaryBaseMax(+0xBC)`
  - `u32 secondaryCurrent(+0xB8)`
  - `u32 secondaryBaseMax(+0xC0)`
  - `u32 extra132(+0x84)`
  - six tagged `u32` values truncated into word slots around `+0x122`
  - `u32 summary176(+0xB0)`
  - `u32 gap09C0(+0x9C)`
  - two `u8` state bytes (one also copied into global scene state)
  - `u32 primaryDisplayMax(+0xC4)`
  - `u32 secondaryDisplayMax(+0xC8)`
  - bounded strings `shortLabel(+0x100)` and `previewImage(+0x10A)`
  - three more `u32` fields at `+0xCC/+0xD0/+0xD4`
  - bounded `actorResource(+0xD8)`, `i16 actorResourceArg(+0xAC)`, bounded global `sceneKey`, then `i16 +0xF0/+0xF4`
- corrected implication: the current scene issues are not just “HP current field guessed wrong”. The prior builder also had the display-max pair and the preview/actor-resource tail in the wrong relative order, which explains why HP, level-like HUD state, preview art, and scene actor bootstrap could all be wrong together.
- changed: rewrote `vm_net_mock_build_actor_info()` to match that newer order instead of the older `u32 level/current/base/...` guess. The current builder now:
  - keeps `visualVariant=job-1`, `visualGroup=sex`
  - uses `summaryStatus` defaulting to `roleLevel`
  - keeps HP/MP as `{current, baseMax, current, baseMax}`
  - moves the display-max pair before the bounded `shortLabel/previewImage` strings
  - splits the old single `gap0CC0` placeholder into three separate trailing `u32` placeholders before `actorResource`
  - uses `roleName` for the second bounded display-name string instead of the old `JHOnline`
- changed: upgraded `1/2/10` from an empty blocker-removal reply into a minimal self-actor seed. Both the bundled scene follow-up and the standalone one-object `WT 1/2/10` path now return `othernum=1` plus one `otherinfo` entry for actor `10001`, with:
  - `visualVariant=job-1`, `visualGroup=sex`
  - `gridPos=(12,10)` / `targetPos=(12,10)` by default, overridable through envs
  - role-name strings for `labelText/shortLabel/longLabel`
- changed: added a narrow runtime parse-exit watcher `trace_scene_actorinfo_snapshot` at `0x0100FD2E` so the next rerun can directly confirm what the scene node actually contains after consuming the rebuilt actor blob.
- validation: rebuilt with `make` after the scene actorinfo reorder correction, non-empty `1/2/10` seed, and the new parse-exit watcher; build passed.

## 2026-06-07
- corrected: the first attempt at that reorder still wrote the first post-string field as `i16` and kept extra phantom node bytes in the stream, which shrank `actorinfo_len` by 2 and immediately broke role-confirm on the next rerun.
- verified by the failing runtime snapshot: `trace_scene_actorinfo_snapshot` on the bad build showed `summaryStatus=29795`, `hp=7864324/7864324`, `mp=6553604/6553604`, plus empty `preview/actor/scene`, which is the expected signature of a width/order mismatch rather than a downstream scene bug.
- changed: corrected `src/main.c:vm_net_mock_build_actor_info()` again so the stream now matches the confirmed parser order more tightly:
  - first post-display-name field is back to tagged `u32 summaryStatus`
  - `primaryDisplayMax/secondaryDisplayMax` are serialized before `shortLabel/previewImage`
  - `displayName` stays separate as `JHOnline`; the later `shortLabel/previewImage` pair remains in their own slots
- validation: rebuilt with `make` after the actorinfo width/order correction; build passed.

## 2026-06-07
- corrected again by full tail decompile plus the latest runtime evidence: the previous 236-byte `actorinfo` build was still 6 bytes too short. `parse_actorinfo_response()` reads two additional tagged `u8` fields after `primaryDisplayMax/secondaryDisplayMax` and before `shortLabel/previewImage`; removing them was incorrect.
- verified by correlation:
  - good historical runs logged `mock_title_role_select_response ... actorinfo_len=242`
  - the still-crashing build logged `actorinfo_len=236`
  - the same crash build also showed the classic “tail missing” snapshot shape: `hp=120/120`, `mp=100/100`, but `summaryStatus=0` and empty `short/preview/actor/scene`
- changed: restored those two tagged `u8` fields in `src/main.c:vm_net_mock_build_actor_info()` as `CBE_ACTOR_BYTE_11E` / `CBE_ACTOR_BYTE_120` immediately after the display-max pair, which should bring the wire blob back to the 242-byte shape.
- validation: rebuilt with `make` after restoring the two post-display-max `u8` fields; build passed.

## 2026-06-07
- verified on the next rerun: the role-select `actorinfo` tail alignment is now back to the previously good shape. `bin/logs/net_packets.log` shows `mock_title_role_select_response ... actorinfo_len=242 len=460`, and the scene no longer dies immediately in `parse_actorinfo_response()`.
- verified: the next crash has moved later into map/scene loading and is no longer an `actorinfo` width problem. The latest stdout fault is `pc=0x01004E1C`, `lr=0x0100F5FD`, and static IDA maps it to `sub_1004E10()`, a tiny selector-index setter that dereferences `*(selector+0x0C)` as a table pointer before reading its `+8` bound field.
- verified by combined runtime/static evidence:
  - live registers at the fault show `r0=0x05400018`, so the selector object itself exists, but its internal table pointer at `+0x0C` is null
  - `lr=0x0100F5FD` lands inside `sub_100F094()`, which is the confirmed parser/consumer family for scene actor/otherinfo records
  - this makes the current failure best described as “scene selector/list object reached a setter before its backing table was initialized”, not as a renewed network parse failure
- changed: added a very narrow blocker-removal guard in `src/main.c` at code-hook entry `0x01004E10`. When `selectorBase != 0` but `*(selectorBase+0x0C) == 0`, the emulator now logs `trace_scene_selector_table_guard`, returns the original selector pointer unchanged, and skips only that one null-table index update.
- rationale: `sub_1004E10()` is already a bounded/no-op-style setter; skipping it when the backing table is absent is narrower than fabricating table contents and keeps the surrounding scene/resource flow intact for the next rerun.
- validation: rebuilt with `make` after the selector-table null guard; build passed.

## 2026-06-07
- verified on the following rerun: the selector-table guard does fire (`trace_scene_selector_table_guard ... selector=05400018 index=1`) and pushes the scene farther into the first steady HUD draw.
- verified: the next crash is later and still purely local. `stdout_trace.log` now ends with `pc=0` / `lastPc=0x010146DA`, and static `scene_draw_status_panels()` confirms `0x010146DA` is the `BLX` of `sceneHudCurrentPortraitWidget->draw(+0x18)` for the left portrait/status block.
- verified by combined runtime/static evidence:
  - all six portrait UI actor resources (`ui_h_war.actor`, `ui_hw_war.actor`, `ui_h_ass.actor`, `ui_hw_ass.actor`, `ui_h_mag.actor`, `ui_hw_mag.actor`) are loaded earlier in the same scene-init pass
  - the actual failing inline call still sees a null draw callback (`R3=0`), so this is not a resource-file miss; it is an uninitialized portrait-widget method slot
- changed: added another narrow blocker-removal guard in `src/main.c` at code-hook entry `0x010146DA`. When `scene_draw_status_panels()` is about to `BLX` a null portrait-widget draw callback, the emulator now logs `trace_scene_portrait_draw_guard` and resumes at `0x010146DC` instead of branching to `0`.
- rationale: this is a single-callsite skip for an optional HUD portrait draw; it preserves the rest of `scene_draw_status_panels()` and should expose the next live blocker without fabricating portrait-widget state.
- validation: rebuilt with `make` after the portrait-draw null-callback guard; build passed.

## 2026-06-07
- verified on the latest rerun: both earlier local guards now fire in sequence (`trace_scene_selector_table_guard` during scene load, then `trace_scene_portrait_draw_guard` during the first `status_panels` pass), and the HUD divide-site values remain healthy at `120/120` and `100/100`.
- verified: the next crash is later in the first `actor_pass`, with `stdout_trace.log` ending at `pc=0x01004EA8`, `lr=0x01045653`, `r4=0x05400018`, and live `R0` becoming `0` exactly at `LDR R0, [R0,#0xC]`.
- static IDA maps `0x01004EA8` to the entry of `sub_1004E9C()`, a selector-backed sprite/tile draw helper. It immediately dereferences `selectorBase+0x0C`, then walks `(*(selector+0x0C)+0x0C)` and row data before dispatching to the draw callback at `selectorOwner[4]+0x40`.
- static IDA also maps `lr=0x01045653` to `scene_draw_node_overhead_overlay()`. That caller loads `R0 = node + 0x18` and `BLX [R0+0x30]`; on the live crash, `node+0x18 == 0x05400018`, which is the same selector object previously seen by the earlier `sub_1004E10()` null-table setter guard.
- changed: added a second narrow selector-null blocker-removal guard in `src/main.c` at code-hook entry `0x01004E9C`. It fires only when `selectorBase != 0`, `*(selectorBase+0x0C) == 0`, and the caller is the confirmed `scene_draw_node_overhead_overlay()` site (`lr & ~1 == 0x01045652`), then logs `trace_scene_selector_draw_guard`, returns `0`, and skips only that one overhead-overlay selector draw.
- rationale: this keeps the fix narrower than a generic selector stub. It does not fabricate selector tables or alter earlier initialization paths; it only suppresses the specific actor-pass overlay draw that currently dereferences the same uninitialized selector object.
- validation: rebuilt with `make` after adding the `0x01004E9C` selector-draw null-table guard; build passed.

## 2026-06-07
- verified on the immediate rerun: the new `trace_scene_selector_draw_guard` does fire once during the first `actor_pass`, exactly at `pc=0x01004E9C`, `lr=0x01045653`, `selector=0x05400018`. So the first scene-node overhead overlay selector dereference is now being skipped as intended.
- verified by the very next stdout session in the same log file: the client then crashes again at the same helper entry `pc=0x01004EA8`, but with a later caller inside the same function family: `lr=0x01045859`, `r7=0x010058E7`. Static IDA keeps both `0x01045653` and `0x01045859` inside `scene_draw_node_overhead_overlay()`.
- static evidence: `scene_draw_node_overhead_overlay()` contains multiple selector-backed draw branches before it returns. The original guard only matched the first callsite (`0x01045652`) and therefore could not catch the second same-function selector draw branch now reached after the first skip.
- changed: widened the `0x01004E9C` guard only from a single callsite to the containing function range. It now fires when `selectorBase != 0`, `*(selectorBase+0x0C) == 0`, and `lr` falls anywhere inside `scene_draw_node_overhead_overlay()` (`0x01045578..0x010459EB`), while still logging the exact live `lr` through `trace_scene_selector_draw_guard`.
- rationale: this remains a scene-local overhead-overlay suppression, not a global selector bypass. It should cover the whole currently confirmed null-selector draw family without affecting unrelated selector users elsewhere in the binary.
- validation: rebuilt with `make` after widening the `scene_draw_node_overhead_overlay()` selector-draw guard; build passed.

## 2026-06-07
- verified on the latest stable scene rerun: there is no new crash. `stdout_trace.log` now ends with only the normal `ScreenResume/ScreenInit` lines, and `net_trace.log` continues through repeated steady-scene `draw_pass -> status_panels -> actor_pass` cycles after `loadFlags` falls to `0,0,0`.
- verified by the new publish trace: the current scene node is not missing or empty. `trace_scene_current_node_publish` fires at `0x0100F7DC` and shows:
  - `current=0x05400000`
  - `snapshot=0x0500C468`
  - `actor=10001/10001`
  - `label=snapshotLabel=侠剑江湖`
  - `short=10001`
  - `occupied=1`
  - `drawAt=0x01045579`, `step=0x01045429`
- verified: the same trace also shows the strongest current explanation for “主角不在地图上 / 左上角有残片 actor 图”:
  - `grid=50,50`
  - `target=0,0`
  - `visual=0,0`
  - `visualRes=0/0`
  Those coordinates are far away from the earlier intended mock defaults `(12,10)` and are not a normal on-screen spawn target.
- implication: the active scene node is being published successfully and carries valid actor identity, callbacks, and occupancy, so the current “missing hero” issue is no longer best described as “node never created”. It is now best described as “the published current node has wrong spatial/visual state”, with `grid=50,50` and `target=0,0` as the first concrete runtime evidence.
- historical verified result for that run: the bundled scene follow-up was no longer an empty-actor wipe and the response log read `mock_scene_resource_followup_response ... othernum=1 actorId=10001 ...`; this is now superseded by the later parser-safe empty `2/10` experiment after the same-window non-empty record corrupted `ActorSceneNode.drawAt`.
- static IDA decompile of `parse_actorinfo_response()` at `0x0100FA88` now removes one ambiguity around that bad spatial state:
  - after the two display-max `u32` fields, fresh scene-enter reads two `u8` values and stores them directly into the snapshot actor structure at `+0x11E/+0x120`
  - those exact slots later appear as `target=%u,%u` in `trace_scene_current_node_publish`
  - the current mock had still been defaulting those two bytes to `0/0` via `CBE_ACTOR_BYTE_11E` / `CBE_ACTOR_BYTE_120`
- changed: tightened `src/main.c:vm_net_mock_build_actor_info()` so that actorinfo now reuses the already existing scene seed knobs instead of separate stale defaults:
  - the two confirmed target bytes now default from `CBE_ACTOR_TARGET_X/Y` (`12,10`) while still allowing the older `CBE_ACTOR_BYTE_11E/120` override names
  - the final trailing `i16/i16` pair now defaults from `CBE_ACTOR_GRID_X/Y` instead of the old hardcoded `50/50`
- rationale: this keeps the wire shape unchanged while aligning the fresh-enter actorinfo seed with the same `(target,grid)` defaults already used by the later `1/2/10 otherinfo` self-node seed. The target-byte mapping is now confirmed by decompile; the trailing `i16` pair remains a strong runtime hypothesis because it is the only remaining actorinfo default that still explained the published `grid=50,50`.
- validation: rebuilt with `make` after the actorinfo target/grid default correction; build passed.
- static IDA for `scene_runtime_init_and_sync()` at `0x010136BC..0x010136DE` now resolves the portrait-widget selection formula too:
  - after building the six 44-byte portrait widgets, the scene code computes `index = visualGroup + 2*visualVariant`
  - then selects `sceneHudCurrentPortraitWidget = portraitWidgetBase + 44 * (index - 1)`
  - this means `visualVariant` is 0-based (`job-1`), but `visualGroup` must be the original `1..2` sex family rather than `0..1`
- verified against the latest bad rerun: live `trace_scene_current_node_publish` still showed `visual=0,0`, and the next `trace_scene_portrait_draw_guard` fired with `widget=0x05402108 drawCb=0`. That matches the off-by-one portrait-bank selection implied by the formula above.
- changed: corrected the scene-facing visual mapping in both `vm_net_mock_build_actor_info()` and `vm_net_mock_append_actor_other_empty10_object()` so `visualGroup = actorSex + 1` while keeping `visualVariant = actorJob - 1`.
- rationale: this does not touch the already accepted title/role-list `sex=0` contract. It only fixes the scene/runtime visual-byte semantics used by portrait-bank and actor-widget selection.
- validation: rebuilt with `make` after the scene visual-group correction; build passed.
- verified on the next rerun: the scene-facing actor seed is now materially healthier, which narrows the remaining “零碎人物图像” issue away from the high-level actorinfo wire shape:
  - `trace_scene_current_node_publish` now shows `grid=12,10 target=12,10 visual=1,0`
  - the earlier `trace_scene_portrait_draw_guard` no longer appears in the newest rerun, so the portrait-bank off-by-one was a real bug and is now fixed
- remaining live mismatch after that correction:
  - the same publish trace still shows `visualRes=0/0`
  - but in the same tick, `scene_rebuild_runtime_nodes()` already starts binding all six main actor resources (`h_warrior.actor`, `hW_warrior.actor`, `h_assassin.actor`, ...) and opening their inner GIF members
  - static `scene_rebuild_runtime_nodes()` then immediately calls `scene_node_refresh_visual()` at `0x0100F82C`
  - static `scene_node_refresh_visual()` reads `node->visualGroup` / `node->visualVariant`, indexes a six-entry table stored at the start of the node, and copies the chosen entry into `node+0x24` (`visualResId`)
- implication: the current actor-response blob is no longer the strongest suspect by itself. The remaining issue now looks more like a local deferred actor-asset bind gap: the `.actor` resources are queued/opened, but `scene_node_refresh_visual()` runs before the selected node-table entry is nonzero, so `visualResId` remains `0`.
- changed: added a narrow runtime fixup in `src/main.c` at the `actor_pass` tick hook. When the current node already has valid `visualGroup/visualVariant`, `visualResId == 0`, and the corresponding six-entry node table slot has since become nonzero, the emulator now copies that slot into `node+0x24` and logs `trace_scene_visual_res_fixup`. If the slot is still zero, it logs `trace_scene_visual_res_still_missing`.
- validation: `make` recompiled `obj/main.o`, but final link to `bin/main.exe` was blocked because the running emulator process still held the file open. The same objects were successfully linked into `bin/main_patched.exe`, confirming the new fixup code builds cleanly.
- verified on the next rerun: the scene still reaches stable `actor_pass` ticks with no new protocol blocker, but the new fixup pair never logs at all even though `trace_scene_current_node_publish` continues to show the same unresolved local state:
  - `grid=12,10`
  - `target=12,10`
  - `visual=1,0`
  - `visualRes=0/0`
- implication: the actor-response side is no longer the best suspect for the remaining “零碎人物图像” symptom. The stronger current suspicion is now inside the local `actor_pass`-time fixup itself: it is returning through an earlier guard (`no current node`, `invalid visual`, etc.) before it reaches either `trace_scene_visual_res_fixup` or `trace_scene_visual_res_still_missing`.
- changed: added a read-only diagnostic probe inside `vm_fixup_current_node_visual_res_if_ready()` so the next rerun will explicitly log why it returns:
  - `trace_scene_visual_res_probe reason=no_r9`
  - `trace_scene_visual_res_probe reason=no_current_node`
  - `trace_scene_visual_res_probe reason=invalid_visual`
  - `trace_scene_visual_res_probe reason=ready ... slots=<6 entry table>`
- rationale: this keeps the existing narrow fixup behavior untouched while making the next actor-pass rerun decisive. We will know whether the current node pointer disappears, the visual bytes get clobbered after publish, or the six-entry actor slot table is still zero when `actor_pass` begins.
- validation: rebuilt with `make` after adding the `trace_scene_visual_res_probe` instrumentation; build passed and updated `bin/main.exe`.
- verified on the next rerun: the new probe resolves the remaining ambiguity around `visualRes`. The stable scene now shows repeated `trace_scene_visual_res_probe reason=ready` during `actor_pass`, and every sample agrees:
  - `node=0x05400000`
  - `visual=1,0`
  - `selectedIndex=1`
  - `visualRes=0x054045A8`
  - `tableEntry=0x054045A8`
  - `slots=054045a8,05406604,054086b4,0540ad14,0540d4b4,0540f034`
- implication: the earlier “fragmented hero sprite because `visualRes` never fills” hypothesis is now disproven. By the first stable `actor_pass`, both the selected six-slot actor table entry and `node+0x24` are already nonzero and consistent.
- correlated evidence already in the same log strengthens that pivot:
  - `trace_sub_1010228_callsite label=scene_runtime_init_and_sync` copies `field36=054045a8` from the live node into the scene-side scratch/render object
  - `trace_scene_current_node_publish` still prints `drawAt=0x01045579 step=0x01045429`
  - static IDA already identifies `0x01045578` as `scene_draw_node_overhead_overlay()`, so the currently surfaced callback pair belongs to the overhead/title family, not necessarily the main body sprite path
- next focus: stop treating the issue as missing actor resource binding and instead trace the actual body/world draw path versus the overhead overlay path. The visible top-left fragment is now more likely a local draw-path/state problem than a missing `.actor` asset slot.
- static IDA now sharpens that split inside `scene_draw_actor_pass()` at `0x01014456`:
  - the early loop over 32-byte move entries calls one of two global body-drawer callbacks (`0x010144D0` / `0x01014510`) after computing screen-space positions from the move-entry box fields
  - the later 25-slot node loop separately invokes each `ActorSceneNode` callback from `node+0x148` and `node+0x14C`
  - for the current self node, those callbacks still read back as `drawAt=0x01045579` and `step=0x01045429`
  - static IDA already identifies `0x01045578` as `scene_draw_node_overhead_overlay()`, so the per-node callback family we have been seeing is more consistent with title/icon/overhead rendering than with the main body/world sprite renderer
- changed: added two narrow runtime traces in `src/main.c` to distinguish those families on the next rerun:
  - `trace_scene_body_draw_dispatch` at `0x010144D0` / `0x01014510` logs the move-entry branch (`type2_body` vs `normal_body`), move-index, raw move-entry box fields, computed screen coordinates, and callback target
  - `trace_scene_current_node_draw` at `0x01014594` / `0x010145AC` logs only when the callback node matches `sceneCurrentNode`, capturing its `visualRes`, `grid/target`, computed screen coordinates, and the active `drawAt/step` callback pointer
- rationale: this keeps protocol and resource-binding behavior untouched while making the next rerun decisive about whether the broken visible fragment comes from the true body/world draw branch or from the current-node overlay family.
- validation: rebuilt with `make` after adding the `trace_scene_body_draw_dispatch` / `trace_scene_current_node_draw` instrumentation; build passed and updated `bin/main.exe`.
- verified on the next rerun: the new split trace makes the current “零碎人物图像/主角不见” problem much narrower.
  - the main body/world branch in `scene_draw_actor_pass()` is definitely executing for the self actor family: `trace_scene_body_draw_dispatch label=type2_body` fires every frame with `moveIndex=0` and callback `0x01004E9D`
  - but that same body branch computes clearly off-screen coordinates for the live move entry: `screen=-221,-479`
  - the raw move-entry box fields feeding that conversion stay stable at `box=203,402,240,422`
- verified: the per-node current-self callbacks are now clearly a separate family from the true body/world draw branch.
  - `trace_scene_current_node_draw label=current_node_drawAt` reports `callback=0x01045579` with `screen=0,-67`
  - `0x01045578` is already identified in IDA as `scene_draw_node_overhead_overlay()`
  - so the top-left visible fragment is now best explained as overhead/title/icon rendering near the screen edge, while the actual body sprite path is being sent far off-screen by bad move-entry/world-to-screen state
- verified: a new real wire-level blocker appears after manual in-scene interaction. The latest `net_packets.log` ends with:
  - `WT len=61 hdr=2/10 objs=1/2/10,1/14/14,1/14/4,1/14/5,1/14/6 count=5`
  - current emulator behavior is still `unhandled_packet ...` followed by `assert(0)`
- implication: the investigation now cleanly splits into two independent threads:
  - local scene-state bug: self body draw is active but off-screen (`screen=-221,-479`)
  - new protocol gap: post-scene interaction packet family `2/10 + 14/14,14/4,14/5,14/6`
- static follow-up on the local thread: `scene_draw_actor_pass()` definitely reads its early body-pass records from the move-entry table rooted at `R9+0x5CE4`, but the current `type2_body moveIndex=0` trace still does not prove that entry `0` is the self actor. `sub_100F094()` is the function that rebuilds that 32-byte move-entry table and returns just before `0x0100F78E`.
- changed: added a narrow read-only parse-exit snapshot at `sub_100F094` tail (`0x0100F78E`). New log keys:
  - `trace_actor_move_entry_table`
  - `trace_actor_move_entry item=...`
  The snapshot dumps the table base, parsed entry count, and the first four raw 32-byte entries (`8 x dword + kind`) immediately after rebuild.
- rationale: this will let the next rerun answer the current strongest local question without guessing field semantics too early:
  - whether `moveIndex=0` is really the self actor
  - which entry actually corresponds to the self actor
  - whether the bad coordinates are already present when `sub_100F094()` finishes, or only arise later during draw-time conversion
- validation: rebuilt with `make` after adding the `trace_actor_move_entry_table` / `trace_actor_move_entry` instrumentation; build passed and updated `bin/main.exe`.
- verified on the next rerun: `sub_100F094()` settles the main remaining body-draw bug much more cleanly than the older visual-resource hypothesis.
  - `trace_actor_move_entry_table` reports `count=1`
  - `trace_actor_move_entry item=0` decodes as a tag-2 move entry with `actorId=1`, not self actor `10001`
  - the same run still publishes `sceneCurrentNode actor=10001 grid=12,10 target=12,10 visual=1,0 visualRes=054045a8`
  - implication: the self actor exists as the active scene node, but never enters the body/world move-entry list that `scene_draw_actor_pass()` uses for the real sprite branch
- static decompile of `sub_100F094()` now confirms why this split matters:
  - only `actorTag == 2` or `actorTag == 9` writes a 32-byte `ActorMoveEntryEx` into the table at `R9+0x5CE4`
  - `actorTag == 4` only updates side-state such as `currentActorId`
  - `actorTag == 3/14` creates scene nodes and resource-name bindings, but not body-pass move entries
- implication: the current `1/2/10 otherinfo` mock was not just “missing one field”; it was the wrong wire family entirely for this parser. The previous ad hoc blob was being misread as a single bogus tag-2 move entry for actor `1`, which matches the stable `moveIndex=0 screen=-221,-479` trace.
- changed: rewrote the `1/2/10 otherinfo` mock builder in `src/main.c` to emit the typed record stream that `sub_100F094()` actually consumes:
  - recordCount `2`
  - one tag-2 self body entry for actor `10001`
  - one tag-4 `currentActorId=10001` record
  - new runtime log text now advertises `records=2 tags=2+4 actorId=10001`
- validation: rebuilt with `make` after the `1/2/10 otherinfo` rewrite; build passed and updated `bin/main.exe`. Next rerun should be judged first by whether `trace_actor_move_entry item=0` flips from `actorId=1` to `actorId=10001`.
- corrected on the next rerun: that `1/2/10 -> sub_100F094()` hypothesis was wrong. Static decompile of `sub_1012958()` (`net_handle_actor_move_info()` case 10) shows `otherinfo` is a compact self-node tuple parsed directly as:
  - `u32 actorId`
  - `u8 visualVariant`
  - `u8 visualGroup`
  - `string labelText`
  - `u8/u8 targetPos`
  - `string shortLabel`
  - `string longLabel`
  - `i16 gridPosX`
  - `i16 gridPosY`
  followed by `scene_node_find_or_create(...)`
- verified with the same rerun:
  - the new `mock_scene_resource_followup_response ... otherinfo=records2(tags2+4 actorId=10001)` did appear
  - but `trace_actor_move_entry_table` was already `count=1` with the same old `actorId=1` entry before that response was even handled
  - implication: `1/2/10 otherinfo` is not the producer of the body/world move-entry table; it only seeds or refreshes the scene node family
- changed: reverted `1/2/10 otherinfo` in `src/main.c` back to the compact `sub_1012958()`-compatible tuple and restored the older log wording. The wrong record-stream experiment is now explicitly retired.
- implication: the strongest remaining local question shifts again. The self actor still publishes correctly as `sceneCurrentNode`, but the body/world move-entry table must be coming from a different source than `otherinfo`, and that upstream source is what still needs tracing.
- changed: added two new narrow traces in `src/main.c` for the next rerun:
  - `trace_actor_move_entry_parser_entry` at `0x0100F094` logs who is invoking `sub_100F094()` and with which stream/cursor pair
  - `trace_actor_move_entry_append` at `0x0100F468` logs each concrete 32-byte entry as it is appended inside the `actorTag == 2/9` path
- validation: rebuilt with `make` after restoring compact `1/2/10 otherinfo` and adding the new move-entry producer traces; build passed and updated `bin/main.exe`.
- verified on the next rerun:
  - `trace_actor_move_entry_parser_entry` fires at `tick=1303` with `lr=0x0100DB3B`, so `sub_100F094()` is being invoked from `parse_actor_motion_descriptor()`
  - the same trace shows `stream=0x0501F640`, which matches the `mock_response ptr` of the bundled `builtin-group-type1` response in the same tick
  - `trace_actor_move_entry_append` then logs the very first and only appended entry, and it is still the same bogus `actorId=1` tag-2 record
- implication: the body/world move-entry table is being built while consuming the earlier bundled scene-enter response (`builtin-group-type1`), not while consuming the later `scene-resource-followup` `1/2/10`
- changed: added one more narrow context trace at `parse_actor_motion_descriptor()` entry:
  - `trace_actor_motion_descriptor_context`
  - next rerun will log its `a5` descriptor-name pointer and `a8` callback pointer so we can tell exactly which descriptor family is driving the bad append
- validation: rebuilt with `make` after adding `trace_actor_motion_descriptor_context`; build passed and updated `bin/main.exe`.
- corrected on the next rerun: the new context trace did not fire because the first hook site was too late in the function body. IDA confirms the real entry of `parse_actor_motion_descriptor()` is `0x0100D6E2`, while `0x0100DB00` was only an internal block.
- changed: moved the context trace hook to the real entry `0x0100D6E2` and also added a second safety hook at `0x0100DB2C`, immediately before the optional callback / `sub_100F094()` handoff branch. Rebuilt with `make`; build passed.
- verified on the next rerun:
  - `trace_actor_motion_descriptor_context` now fires at the real entry
  - the active descriptor name is `c00蓬莱仙岛_01`
  - `a8=0x0100F095`, i.e. the callback passed into `parse_actor_motion_descriptor()` is `sub_100F094+1`
  - the caller is `0x010352D6`, which IDA currently places inside `scene_hud_main_panel_init()`
  - the later `0x0100DB2C` safety hook also fires, but by then the outgoing callback slot has already been normalized away and no longer carries the original context
- implication: the bogus body/world move-entry is not being synthesized from a random follow-up packet field. It is being generated by the map descriptor pipeline for `c00蓬莱仙岛_01`, with `sub_100F094()` explicitly passed as the descriptor callback.
- verified via cross-check between runtime logs and local asset parsing: the bogus `moveIndex=0` body entry numerics now line up exactly with the first edge-portal in `bin/JHOnlineData/c00蓬莱仙岛_01.sce`. The logged entry words decode to `actorId=1`, `223,382`, `203,402`, `240,422`, and `tools/inspect_sce.py` reports that same scene file contains an edge portal with `spawn_point=(223,382)` and `trigger_rect=(203,402,240,422)`.
- implication: the strongest current reading is no longer just “wrong descriptor family somewhere upstream”. More specifically, the scene-key descriptor path is handing portal/transition geometry into the later `sub_100F094()` consumer family, which then turns it into the lone bogus type-2 body/world entry.
- changed: added one more narrow handoff trace in `src/main.c` at `0x0100DB2C`:
  - `trace_actor_motion_callback_handoff`
  - it logs the callback handoff stream/cursor, the parser-local section counts, the first 8-byte tuple table entry, the first `count20` payload bytes, and the first 8 shorts at the exact tail that `sub_100F094()` will consume next
- validation: rebuilt with `make` after adding `trace_actor_motion_callback_handoff`; build passed and updated `bin/main.exe`.
- verified on the next rerun: the callback-handoff trace makes the grammar collision explicit. At `tick=167`, `trace_actor_motion_callback_handoff` logs:
  - `cursor=135`
  - `tailShorts=3,2,223,382,8,1,5,1`
  - `tailHex=03 00 02 00 df 00 7e 01 08 00 01 00 05 00 01 00`
  - immediately after that, `trace_actor_move_entry_parser_entry` re-enters `sub_100F094()` with the same `stream=0501f640 cursor=135`
- implication: `sub_100F094()` is not just “eventually producing portal-like numbers”. It is literally starting on the raw `.sce` portal token sequence itself, interpreting:
  - `3` as actor-record count
  - `2` as actorTag
  - `223,382` as grid/point
  - `8,1,5,1,...` as its own property grammar
- corroborating evidence: the active resource family still has no companion `c00蓬莱仙岛_01.*` descriptor besides `c00蓬莱仙岛_01.sce`, so the current path is not accidentally opening a same-base-name alternate file from disk.
- verified by static follow-up on `scene_hud_main_panel_init()` / `scene_rebuild_runtime_nodes()`:
  - `scene_hud_main_panel_init()` (`0x010352AE`) does not choose the resource name or callback itself; it just forwards its register args plus three stacked extras into the generic initializer at `*(R9+0x5C58)+0x10`, then installs HUD-local callbacks
  - the decisive pairing happens one level earlier in `scene_rebuild_runtime_nodes()` (`0x0100F8A8..0x0100F8F6`)
  - when `varg_r0 != 0`, the function first copies `currentMapIdText` into `parserNodeOrScenePtr->currentName` (`0x0100F8B8..0x0100F8DC`)
  - it then always queues the same extra stack tuple for `scene_hud_main_panel_init()`:
    - arg0 = pointer to that copied `currentName`
    - arg4 = constant `10`
    - arg8 = `sub_100F094+1`
- implication: the newest static evidence now points more strongly to a wrong resource-name source than to a downstream callback-selection bug. The `sub_100F094` callback is already fixed by the caller at `0x0100F8E0..0x0100F8EA`; `scene_hud_main_panel_init()` merely forwards it.
- corroborating contrast:
  - `sub_100F094()` itself has another callsite into `scene_hud_main_panel_init()` at `0x0100F702`
  - `scene_hud_main_panel_sync_message()` has a third callsite at `0x010368C4`
  - both of those callers also choose the optional `sub_100F094` callback explicitly in their own stack setup, which makes the callback family look intentional/caller-selected rather than accidentally inferred by `scene_hud_main_panel_init()`
- verified on the next static pass: the `currentMapIdText` source is the same persistent scene-key slot already populated from `actorinfo`. In `scene_runtime_init_and_sync()`:
  - `R6 = R9 + 0x5E46`
  - `sub_100EEBC(R9+0x5E46)` validates it
  - `sub_100FA40(R9+0x5E46)` runs immediately before the `scene_rebuild_runtime_nodes(..., currentMapIdText)` call
  - that same `R9+0x5E46` pointer is then stored as the stack `currentMapIdText` argument
- implication: the live misroute is now fully closed in static code:
  - trailing `actorinfo` scene-key field -> `R9+0x5E46`
  - `scene_runtime_init_and_sync()` forwards `R9+0x5E46`
  - `scene_rebuild_runtime_nodes()` copies it into `parserNodeOrScenePtr->currentName`
  - same caller pairs it with `sub_100F094+1`
  - local open resolves it as `c00蓬莱仙岛_01.sce`
  - callback then parses raw portal tokens as body-entry grammar
- verified by the next full `parse_actorinfo_response()` pass: the actor-facing string inventory is now effectively exhausted, and there is no obvious spare fourth “motion/body descriptor name” field hiding later in the blob. On the fresh-enter (`a2==0`) path the parser reads:
  - role/name string -> copied via `v27/v29` into actor `+0x44`-family and mirrored into globals `R9+24040` / `R9+24072`
  - 20-byte bounded string at `actor+0x6C` (`currentSceneNode + 108`), which current runtime snapshots show as `label=JHOnline`
  - 10-byte bounded string at `actor+0x100`, currently traced as `short=10001`
  - 20-byte bounded string at `actor+0x10A`, already confirmed as `preview=h_warriorwalk1.gif`
  - 20-byte bounded string at `actor+0xD8`, already confirmed as `actor=h_warrior.actor`
  - final 30-byte global string at `R9+0x5E46`, already confirmed as `scene=c00蓬莱仙岛_01`
- implication: the current “scene-key got paired with the callback” bug is no longer best explained by “we missed one more actorinfo string field”. Static actorinfo parsing now leaves no equally sized later string candidate that obviously belongs to the `{ namePtr, 10, sub_100F094+1 }` path.
- verified by a deeper static pass on the two surviving `scene_hud_main_panel_init()` callers:
  - `scene_hud_main_panel_init()` does not forward just three stacked values; its prologue explicitly repacks a four-word tuple for the shared initializer at `R9+0x5C58`:
    - `arg0` -> first extra word (`namePtr`)
    - `arg4` -> second extra word (`mode`)
    - hardcoded `0` -> third extra word
    - `arg8` -> fourth extra word (`optional callback`)
  - evidence: `0x010352B4..0x010352D4` loads caller stack extras, stores `0` into local `var_20`, and then `BLX`es `R9+0x5C58`
  - this tightens the generic-init contract from a vague `{ namePtr, 10, callback }` into the more exact `{ namePtr, mode, 0, callback }`
- verified: `mode=10` itself is not unique to the bad fresh-enter path.
  - `sub_100F094()` has a second `scene_hud_main_panel_init()` call at `0x0100F702`
  - that caller pushes the same `mode=10`, but sets the fourth extra word / callback slot to `0`
  - evidence: `0x0100F6EE..0x0100F702` stores `{ [R9+0x5C64+0x74], 10, 0 }` into the outgoing stack tuple before calling `scene_hud_main_panel_init()` on the secondary `0x694` HUD panel at `R9+0x5CC8`
- implication: the newest static evidence weakens the old “mode 10 itself is the wrong family” reading.
  - stronger current reading: the live bug is specific to the fresh scene-enter pairing of `currentMapIdText / scene-key` with a non-null `sub_100F094+1` callback
  - mode `10` survives elsewhere in the same panel family when the callback slot is explicitly null, so the suspicious axis is now `scene-key + callback`, not just `mode 10`
- verified on the same pass: the alternate `mode=10, callback=0` caller is not reusing the scene-key slot.
  - inside `sub_100F094()`, property kind `0x12` stores a parsed string into `R9+0x5C64+0x74`
  - property kind `0x16` independently stores the centered HUD status text into `R9+0x5C64+0x78`
  - evidence: `0x0100F63E..0x0100F69C` branches `kind 0x12 -> [R9+0x5CD8]` and `kind 0x16 -> [R9+0x5CDC]`; the later `0x0100F6E6..0x0100F702` panel-refresh call loads its `namePtr` from `[R9+0x5CD8]`, not from `sceneKey`
- implication: the codebase now exposes a concrete second string slot already living in the same panel family.
  - strongest current local split:
    - fresh scene-enter currently feeds `sceneKey(R9+0x5E46)` into `mode=10 + sub_100F094`
    - later scene-local refresh feeds the `field-0x12` string at `R9+0x5CD8` into the same `mode=10` family with callback disabled
  - this makes “fresh-enter used the wrong name slot” a stronger hypothesis than before
- verified by a narrower static pass on `sub_100F094()` and the `R9+0x5CD8` consumers:
  - the `field kind 0x12 -> R9+0x5CD8` write only exists in the `actorTag=5/6/7` branch of `sub_100F094()`, the same branch that creates special scene nodes and assigns prompt-template kinds `14/15/11`
  - evidence: `0x0100F100..0x0100F166` constructs the node, sets `sceneNode->promptTemplateKind`, and later `0x0100F68E..0x0100F6A0` stores the parsed string into `R9+0x5CD8`
  - the same branch also stores centered status text separately via `field kind 0x16 -> R9+0x5CDC`, so `0x12` is not the ordinary status-line text slot
- verified: `R9+0x5CD8` participates in prompt / hotspot selection flow rather than in base scene-key flow.
  - `sub_10183A0()` only yields a candidate scene node when `R9+0x5CD8 != 0`, the transient prompt-node pool is idle, and the candidate node has the expected active flags
  - evidence: `0x010184D8..0x01018506`
  - the later `scene_hud_main_panel_init(..., mode=10, callback=0)` refresh path in `sub_100F094()` uses this same `R9+0x5CD8` slot as `namePtr`
- implication:
  - strongest current reading is now that `R9+0x5CD8` is a transient prompt / hotspot / action-label name source produced by `actorTag=5/6/7` scene nodes
  - that makes the fresh-enter pairing of persistent `sceneKey(R9+0x5E46)` with the same panel family look even more suspicious
- verified by an additional write-order pass: `R9+0x5CD8` is not available early enough to serve as the first fresh-enter replacement for `sceneKey`.
  - exact static writer search now shows the only confirmed write to `R9+0x5C64+0x74` is `sub_100F094()` at `0x0100F69C`
  - the commonly nearby `sub_1013D46()` write is to `R9+0x5CE4+0x74` (`0x5C64 + 0x80 + 0x74`), i.e. a different UI-layout struct, not the `0x5CD8` string slot
  - evidence: targeted IDA instruction search over `[#0x74]` stores with `0x5C64` base; only `sub_100F094()` hits the plain `R9+0x5CD8` slot
- verified on the same static pass: in the fresh scene-enter loop, `scene_runtime_init_and_sync()` normalizes `sceneKey(R9+0x5E46)` and immediately calls `scene_rebuild_runtime_nodes(..., currentMapIdText=R9+0x5E46)` at `0x01013586`
  - that happens before any later prompt-local `R9+0x5CD8` data could exist, because `0x5CD8` is only produced by the downstream `sub_100F094()` callback family itself
- implication:
  - the current first-call misroute cannot be fixed simply by “preferring `R9+0x5CD8` instead of `sceneKey` on the very first fresh-enter invocation”, because that buffer does not exist yet
  - strongest current local split is now:
    - either the first fresh-enter `mode=10` call should *not* carry a non-null `sub_100F094` callback at all
    - or it should source its initial `namePtr` from some third producer outside both trailing `sceneKey` and late prompt-local `0x5CD8`
- verified by comparing all currently known `scene_hud_main_panel_init()` callback producers:
  - `scene_rebuild_runtime_nodes()` hardcodes `{ sceneKey/currentMapIdText, mode=10, callback=sub_100F094+1 }` for the first main-panel init
  - `sub_100F094()` later hardcodes `{ R9+0x5CD8, mode=10, callback=0 }` for the secondary refresh path
  - `scene_hud_main_panel_sync_message()` case 6 can replay either variant, but it does not originate the timing itself; it merely chooses callback `0` vs `sub_100F094+1` from an incoming flag byte (`n3`) and replays a stored tuple
  - evidence: `0x010368A2..0x010368C4`
- verified: that `scene_hud_main_panel_sync_message()` replay path is downstream of a temp-data/resource completion stage, not the first fresh-enter source.
  - its sole caller is `sub_1037000()` at `0x01037094`
  - `sub_1037000()` runs after `sub_10368CA(\"MMORPGTempbin\", ...)` commits the temp data file into game data and then invokes `scene_hud_main_panel_sync_message()`
  - evidence: `0x01037086..0x01037094`
- implication:
  - among currently confirmed paths, the first independent producer of a non-null `sub_100F094` callback into the panel family is still the fresh-enter `scene_rebuild_runtime_nodes()` call
  - later `scene_hud_main_panel_sync_message()` activity looks more like replay / reinitialization after resource-temp commit than a competing root cause
- verified by a deeper replay-layout pass on `scene_hud_main_panel_sync_message()`:
  - the panel/message object at `R9+38284` persists a compact replay record that is later copied back out by `scene_hud_main_panel_sync_message()`
  - replayed fields are:
    - `type` at `+0`
    - a `0x1E` byte payload blob at `+2`
    - `currentActorName/namePtr` at `+36`
    - `varg_r3` at `+40`
    - `n19202288` at `+44`
    - callback-choice flag `n3` at `+48`
    - auxiliary handler/function pointers at `+60/+64`
  - evidence: `0x01036794..0x010367C4`
- verified: case 6 replay does not invent any new callback semantics. It simply rebuilds the same `scene_hud_main_panel_init()` stack tuple from that persisted record:
  - if `n3 == 0`, it replays `callback = 0`
  - if `n3 != 0`, it replays `callback = sub_100F094+1`
  - evidence: `0x010368A2..0x010368C4`
- implication:
  - the replay family now looks like a pure serializer/replayer for already-chosen panel-init tuples, not a fresh policy decision point
  - this strengthens the local reading that the *original* policy error still sits in the first `scene_rebuild_runtime_nodes()` producer
- verified by following the temp-resource completion path one step earlier:
  - `sub_10368CA()` is the current shared pack/normalize stage that prepares the two buffers later consumed by the panel replay family
  - after optional temp-file commit, it formats a resource/path string into local `var_294`, normalizes that into `var_114`, normalizes the second caller-supplied buffer into `var_214`, and then passes both into `(*(R9+0x4D68)+0x28)(2, var_114, var_214)`
  - evidence: `0x010368F4..0x010369CA`
- verified: both known update-completion callers feed that same `sub_10368CA()` packer before any replay happens:
  - `sub_1036F48()` for `MMORPGTempcbm`
  - `sub_1037000()` for `MMORPGTempbin`, followed immediately by `scene_hud_main_panel_sync_message()`
  - evidence: `0x01036FBC..0x01036FCA` and `0x01037086..0x01037094`
- implication:
  - the previous `R9+0x4D68/+0x28 = replay-record write` hypothesis is now disproven
  - `scene_hud_main_panel_sync_message()` still reads a persisted replay tuple from `R9+38284`, but that tuple is not currently explained by `sub_10368CA()`
- verified by a follow-up file-op pass on the `R9+0x4D68` object:
  - `startup_screen_commit_temp_data_file_into_game_data()` uses `(*(R9+0x4D68)+0x08)(2, normalizedDest)` as an existence test and `+0x24` as the follow-up delete/remove path on the same normalized destination
  - `sub_10370C2()` uses `R9+0x4D68` slots `+0x10/+0x18/+0x20` in the same `mmorpgTempdata` family, matching temp-file read/write/size style operations rather than HUD replay serialization
  - `sub_10368CA()` now reads much more narrowly as:
    - build a normalized destination path in `var_114`
    - normalize the caller-supplied second path/string into `var_214`
    - call `(*(R9+0x4D68)+0x28)(2, var_114, var_214)`
  - evidence: `0x0103A5C0..0x0103A5D8`, `0x010370F4..0x01037136`, and `0x010368CA..0x010369CA`
- implication:
  - `R9+0x4D68` is a file/path manager family, and slot `+0x28` is now best read as an install/move/copy operation between normalized paths, not as the writer for the main-panel replay tuple
  - the remaining replay-record seed should be chased back into the `R9+0x9588` queue/controller that owns `scene_hud_main_panel_sync_message()`'s current record, not through `sub_10368CA()`
- verified by following the `R9+0x9588` owner object itself:
  - `sub_1037880()` initializes a 72-byte controller at `R9+38280` with:
    - `+0x38 = sub_10365F0`
    - `+0x3C = sub_10366AC`
    - `+0x40 = send_update_chunk_request`
    - `+0x44 = handle_version_update_response`
    - `+0x48 = sub_10376E2`
  - evidence: `0x01037880..0x010378DE`
- verified: `sub_10365F0()` is the general record writer for the current `R9+38284` entry that later gets replayed by `scene_hud_main_panel_sync_message()`
  - it allocates/fills a record and writes:
    - `type` at `+0`
    - zeroed/copy-filled payload at `+2..+0x1F`
    - optional `a4`/resource field at `+0x24` for record types `1/4/5/6`
    - four halfword params at `+0x28/+0x2A/+0x2C/+0x2E`
    - callback-choice byte from `argC` at `+0x30`
    - pointer/context pair at `+0x3C/+0x40`
  - `sub_10366AC()` is the narrower subtype-3 writer that zeroes the payload, copies a short string into `+2`, sets byte `+0x30 = 3`, and publishes the new record as `R9+38284`
  - evidence: `0x010365F0..0x010366A8` and `0x010366AC..0x01036708`
- implication:
  - the callback-choice byte replayed by `scene_hud_main_panel_sync_message()` is now concretely seeded by `sub_10365F0()` / `sub_10366AC()` through the `R9+0x9588` controller family
  - the remaining static task is to identify which caller first invokes controller slot `+0x38` with the fresh-enter main-panel tuple
- verified by following the `get_net_manager_object()` callsites:
  - `sub_100D3EE()` falls back to `get_net_manager_object()+0x38` with record type `1` when an asset/resource name is not available locally
  - `sub_100DB82()` falls back to the same writer with record type `4`
  - `sub_100D564()` falls back to the same writer with record type `5`
  - most importantly for the current scene/bootstrap thread, `parse_actor_motion_descriptor()` falls back to the same writer with record type `6`
  - evidence: `0x0100D452..0x0100D47E`, `0x0100DE86..0x0100DEB0`, `0x0100D6BC..0x0100D6DE`, and `0x0100DB4A..0x0100DB74`
- verified by full decompile of the `parse_actor_motion_descriptor()` tail:
  - on the local-success path it invokes the optional callback `a8(...)` and returns
  - on the fallback path it does **not** go through `scene_hud_main_panel_init()` or the generic panel initializer first
  - instead it directly queues a type-6 record via:
    - `get_net_manager_object()+0x38`
    - `sub_10365F0(6, v46, a5, a1, *(R9+23240), v44, v45, a8 != 0)`
  - evidence: decompile of `0x0100D6E2`, tail `0x0100DB4A..0x0100DB74`
- corrected by re-reading `scene_hud_main_panel_sync_message()` case 6 against `scene_rebuild_runtime_nodes()`:
  - our earlier field labels for the type-6 replay record were too eager
  - record `+0x24` is **not** the later `namePtr`; it is the `scene_hud_main_panel_init()` `R0` object pointer, i.e. the main `0x694` HUD panel/controller instance
  - the replayed `namePtr` instead comes from the copied payload blob at record `+0x02..+0x1F`
    - `scene_hud_main_panel_sync_message()` copies that blob to `SP+var_5C+2`
    - then case 6 passes the address of that copied payload buffer as the first stacked extra argument to `scene_hud_main_panel_init()`
  - evidence: `0x0103678E..0x010367A8`, `0x010368A2..0x010368C4`, and `0x0100F8E8..0x0100F8F6`
- verified field alignment for the fresh-enter type-6 writer:
  - `parse_actor_motion_descriptor()` fallback seeds the record as:
    - `type = 6`
    - payload `+0x02 = a5` (the descriptor/name string, currently `c00蓬莱仙岛_01`)
    - object pointer `+0x24 = a1` (the main HUD panel/controller object)
    - halfword quartet `+0x28..+0x2F = { low/high16(a2), low/high16(a3) }`
    - callback-choice byte `+0x30 = (a8 != 0)`
    - 4th register arg `+0x3C = a4`
    - aux/context field `+0x40 = *(R9+0x5AC8)`
  - on replay, case 6 reconstructs:
    - `R0 = record+0x24` (panel object)
    - `R1/R2 = packed ints rebuilt from +0x28..+0x2F`
    - `R3 = record+0x3C`
    - stacked extra0 = copied payload buffer (`namePtr`)
    - stacked extra1 = constant `10`
    - stacked extra2 = `0` or `sub_100F094+1`, chosen from byte `+0x30`
  - evidence: `0x010365F0..0x010366A8`, `0x0103678E..0x010368C4`, and `0x0100F8E0..0x0100F8F6`
- implication:
  - the callback-choice byte later replayed by `scene_hud_main_panel_sync_message()` case 6 is seeded earlier than the generic panel initializer path and comes straight from `parse_actor_motion_descriptor()` as `a8 != 0`
  - this narrows the root-cause search again: for the fresh-enter main-panel replay family, the decisive writer is now the descriptor/parser fallback path itself, not `sub_10368CA()` and not yet-proven direct persistence inside `[R9+0x5C48]+0x10`
  - the highest-value unresolved semantic slots are now:
    - what `a2/a3/a4` concretely mean in the `scene_hud_main_panel_init()` register contract
    - what `*(R9+0x5AC8)` at record `+0x40` is used for in the wider replay family
- static narrowing from full decompile export (`samples/raw/ida_decompile_temp/`) resolved one of those slots:
  - `*(R9+0x5AC8)` is not a free-form runtime context blob; it is the parser-method-table pointer stored by `sub_100DEB4()`
  - `sub_100DEB4()` builds the scene descriptor/parser bundle at `R9+0x5AC4` and writes:
    - `R9+0x5AC8 = a1` (handler/method table pointer)
    - `R9+0x5ADC = a2`
    - `R9+0x5AE0 = a3`
  - evidence: `0x0100DEB4..0x0100DF80`, especially `STR R4, [R5,#4]`, `STR R1, [R5,#0x18]`, and `STR R2, [R5,#0x1C]`
- the fresh-enter producer side is also narrower than before:
  - `scene_runtime_init_and_sync()` does not invent the type-6 replay `a2/a3` out of packet text; just before `scene_rebuild_runtime_nodes()` it reads two signed halfwords from the local actor/UI controller hanging off `R9+0x5C64+0x44`
  - the exact source is the pair later decompiled as `(__int16)*...gap_1C[8]` and `(__int16)*...gap_1C[12]`, corresponding to the `+0x30/+0x34` reads at `0x010134D2..0x01013582`
  - `scene_rebuild_runtime_nodes()` then reuses those same register values when it primes the hidden extras `{ namePtr=currentMapIdText, 10, sub_100F094+1 }` and calls `scene_hud_main_panel_init()`
  - implication: the queued type-6 record's halfword quartet comes from a local controller/viewport-style pair, not from the scene-key string and not from `1/2/10 otherinfo`
- the replay consumer side is now narrower too:
  - `scene_hud_main_panel_sync_message()` case 6 uses only:
    - record `+0x24` as the HUD panel object (`R0`)
    - record `+0x28..+0x2F` as the rebuilt local ints (`R1/R2`)
    - record `+0x3C` as replay `R3`
    - record `+0x30` as the callback-choice byte (`0` vs `sub_100F094+1`)
    - payload copy `+0x02..+0x1F` as stacked `namePtr`
  - it does **not** read record `+0x40` anywhere on the case-6 path
  - evidence: `0x010368A2..0x010368C4` full disassembly/decompile of `scene_hud_main_panel_sync_message()`
- implication:
  - for the current fresh-enter `sceneKey -> sub_100F094` bug, record `+0x40` is no longer a first-order suspect on the replay side
  - the higher-value unresolved tuple is now the local triple `{ +0x28..+0x2F, +0x3C }`, because that is what case 6 really feeds back into `scene_hud_main_panel_init()`
- neighboring local evidence points to the same source object family being a scene-status snapshot/controller bundle rather than a generic HUD panel:
  - `sub_1010228()` zeroes the `R9+0x5CE4` scratch summary block, then copies multiple fields out of the nearby `R9+0x5C64+0x40/0x44` pointer family:
    - `+0x24`
    - `+0x44` string
    - `+0x64`
    - `+0x80 + 0x34/0x38`
    - `+0xC0 + 0x04/0x08`
    - two status bytes at `+0x140/+0x141`
  - evidence: `0x01010228..0x0101029C`
  - implication: the `{ a2/a3, a4 }` tuple driving case-6 replay now looks even more like a snapshot/status-controller-local viewport/state tuple, not a resource-loader or packet-parser contract
- `a4` is now resolved too on the original fresh-enter producer path:
  - `scene_rebuild_runtime_nodes()` loads `R4 = R9 + 0x5F08` at `0x0100F89E..0x0100F8A6`
  - it then passes that same pointer as:
    - `R3` into `scene_hud_main_panel_init()` at `0x0100F8F0`
    - `R1` into `scene_hud_prompt_grid_init()` at `0x0100F908`
  - implication: type-6 record `+0x3C` is not another scalar/flag; it is a pointer into the shared scene HUD actor-asset/controller root based at `R9+0x5F08`
- the packed local ints now have a narrower business shape:
  - `scene_runtime_init_and_sync()` seeds:
    - `varg_r3_1 = packed s16 pair {0, 67}`
    - `n19202288_1 = packed s16 pair {240, 293}`
  - those exact packed pairs flow through `scene_rebuild_runtime_nodes()` into both:
    - `scene_hud_main_panel_init()`
    - `scene_hud_prompt_grid_init()`
  - `scene_hud_prompt_grid_init()` immediately stores the first pair into local object shorts `+0xA/+0xC` and mirrors their negatives into `+0x2/+0x4`, which is strong evidence that the pair family is a local viewport/window origin+extent tuple rather than arbitrary gameplay numbers
  - evidence: `0x0101301C..0x0101302C`, `0x0100F7A6..0x0100F90A`, and `0x01046CB2..0x01046CD6`
- the producer chain for those packed ints is now one hop less “local-default” than before:
  - `parse_actorinfo_response()` fresh-enter path uses `v5 = *(R9+23720)` as the destination object, i.e. the same `sceneStatusSnapshotNode` later copied into `sceneCurrentNode`
  - after it writes the 30-byte scene key at `R9+0x5E46`, it immediately reads two final `v34()` dwords from the actorinfo stream and stores them at `sceneStatusSnapshotNode + 240/+244`
  - later `scene_runtime_init_and_sync()` forwards the low 16 bits of those same two dwords as the `varg_r3_1` / `n19202288_1` packed pairs used by `scene_rebuild_runtime_nodes()`
  - evidence:
    - `parse_actorinfo_response()` at `0x0100FD10..0x0100FD22`
    - `scene_runtime_init_and_sync()` at `0x010134D2..0x01013586`
  - implication:
    - the `{0,67}` and `{240,293}` values currently seen on the fresh-enter path are not just hardcoded local defaults; they are the low-halfword view of the actorinfo blob's final two trailing `i32` fields
    - the next precise semantic question is whether those two trailing actorinfo dwords are genuinely supposed to encode a viewport/window rect, or whether our mock is still seeding the right slots with placeholder values
- corrected by a deeper static pass on `parse_actorinfo_response()` and `scene_rebuild_runtime_nodes()`:
  - the previous “two trailing actorinfo `i32`” reading was too strong
  - the final two fresh-enter scalars come from `v34()`, the same helper used just above for `actorResourceArg`; static decompile/disassembly therefore makes them much more likely to be signed-16 reads widened into dword storage at `sceneStatusSnapshotNode + 240/+244`
  - evidence:
    - `parse_actorinfo_response()` decompile/disassembly at `0x0100FCEA..0x0100FD22`
    - same helper family one field earlier at `0x0100FCEA..0x0100FCFE`
- corrected by the same pass on `scene_rebuild_runtime_nodes()`:
  - the fresh-enter values derived from `sceneStatusSnapshotNode + 240/+244` do **not** flow into `scene_hud_main_panel_init()` or `scene_hud_prompt_grid_init()`
  - `scene_hud_main_panel_init()` gets `R3 = R9+0x5F08` plus the older `varg_r3 / n19202288` pair, and `scene_hud_prompt_grid_init()` likewise gets `R1 = R9+0x5F08`, `R2 = varg_r3`, `R3 = n19202288`
  - the snapshot-derived pair only survives as extra `R1/R2` values on the `scene_node_reset_at()` calls inside the node-clone loop, and static `scene_node_reset_at()` itself ignores those registers entirely
  - evidence:
    - `scene_runtime_init_and_sync()` at `0x010134D2..0x01013586`
    - `scene_rebuild_runtime_nodes()` at `0x0100F832..0x0100F90A`
    - `scene_node_reset_at()` decompile/disassembly at `0x010459EC..0x01045A94`
- implication:
  - the old local-tuple hypothesis should now be downgraded for the active `sceneKey -> sub_100F094` bug
  - current strongest root-cause evidence is back to the bad `{ namePtr = currentMapIdText/sceneKey, mode = 10, callback = sub_100F094+1 }` pairing itself, not to the actorinfo tail pair around `+240/+244`
- corrected by a scene-bootstrap ownership pass:
  - the function-table writes inside `scene_object_vtable_init()` at offsets like `a1 + 1032 .. a1 + 1088` are **not** the fixed global `R9+0x5C48` ops descriptor that `scene_hud_main_panel_init()` reads
  - evidence:
    - `scene_system_bootstrap()` first allocates the scene object pointer into `[R9+0x54AC]`, then calls `scene_object_vtable_init(*[R9+0x54AC])` at `0x010038CA..0x010038CC`
    - therefore later writes in `scene_object_vtable_init()` are heap-local substructures under that allocated scene object, not direct stores into the fixed global block returned by `scene_get_actor_asset_ops_descriptor()`
    - `scene_get_actor_asset_ops_descriptor()` still returns the raw global address `R9+0x5C48` (`0x01018058..0x0101805C`)
- implication:
  - do not map `scene_hud_main_panel_init()`'s indirect `BLX [R9+0x5C58]` target to heap-local scene-object slots like `sub_1019A0A` / `sub_1019A50`; that earlier inference is not supported
  - the real writer/initializer for the fixed global `R9+0x5C48` ops descriptor remains an open static target
- tightened by a direct reader scan plus loader follow-up:
  - a narrow whole-binary immediate scan for `0x5C48` now shows only the already-known reader family plus literal-pool echoes:
    - `scene_runtime_init_and_sync()` at `0x0101305C/0x0101369C`
    - `scene_get_actor_asset_ops_descriptor()` at `0x01018058`
    - `sub_10183A0()` literal pool at `0x0101841C`
    - `scene_hud_main_panel_init()` at `0x010352C0`
    - `scene_actor_asset_slot_table_load_entry()` at `0x01044DB8/0x01044E24/0x01044ED2`
    - `scene_draw_node_overhead_overlay()` at `0x0104569A`
  - no new main-binary write site for the base `R9+0x5C48` block was exposed by that scan
  - a second constructor-shape sweep also only found writers for other scene/bootstrap blocks, such as:
    - `sub_10035E6(R9+0x55AC)` event/stream method table
    - `sub_1048FEE(R9+0x5EF0)` local scene helper forwarder
    - `sub_1031790(R9+0x7CB4 + n*0x14)` large callback bank
    - `scene_actor_asset_slot_table_init(R9+0x5FD0, 7)` reusable actor-asset slot table
  - evidence:
    - `find_imm(0x5C48)` hit list from IDA Python
    - constructor-shape sweep for contiguous `STR [obj+8/+C/+10/+14]` patterns, then caller review on the scene/bootstrap subset
- new stronger loader-side implication:
  - in the emulator loader, the main CBE code is copied to `ROM_ADDRESS`, then the parser-selected module data bytes at `fileBuffer + g_cbeInfo.BssDataOffset` are copied to `ROM_ADDRESS + g_cbeInfo.headerInt2`, and only after that `Global_R9` is set to `ROM_ADDRESS + g_cbeInfo.headerInt2`
  - evidence:
    - emulator loader source at `src/main.c:9141-9146` and `src/main.c:9158`
    - parser layout in `src/cbeParser.c:80-124`
  - implication:
    - `R9+0x5C48` is part of the module's copied small-data / data region, not part of the separately allocated scene object at `[R9+0x54AC]`
    - current strongest hypothesis is now that the shared actor-asset ops descriptor is image-seeded or loader-relocated data, not a scene-local runtime constructor product inside `江湖OL.CBE`
    - the next best target is therefore to identify the initial callback values already present in the copied `R9` data image, rather than to keep searching for a missing scene-bootstrap writer in the main binary

## 2026-06-07 late rerun: progress strip and portal-fragment split

- user reran after removing host-side active game-state advancement. New visible state: main actor is visible, a center progress strip appears, and actor-image fragments remain near the actor.
- confirmed network evidence:
  - latest `net_packets.log` sequence still reaches `builtin-group-type1`, `builtin-game-type` 2/3, then `builtin-scene-resource-followup`.
  - after the latest `WT len=49` scene-resource follow-up request, there is no newer outbound WT request or unhandled/assert marker in the packet log.
- corrected interpretation:
  - do not state that the strip is only a harmless local leftover. A correct server response should naturally drive the client into a state where the strip is gone.
  - current best wording: no new packet is visibly pending, but the accepted scene responses may still be semantically too weak. Runtime still reports `pendingResync=1`, and the `30/3` / `30/7` room and roles handlers are the next field-level suspects.
- static evidence:
  - `parse_scene_npcinfo_list()` (`0x01039372`) reads `curpage`, `pagenum`, `colnum`, `colnames`, `roomnum`, `roomlist`, `npcnum`, and `npcinfo`.
  - `sub_1039430()` (`0x01039430`) reads `roomid`, `colnum`, `colnames`, `rolenum`, and `rolesinfo`.
  - both handlers clear the loading-widget gate (`R9+0x5530`) at their exit, but only populate list tables when their raw list stream is present and parseable.
- runtime evidence:
  - progress strip source is still `sub_1004362()` tiling through `sub_1004150()` (`trace_progress_strip_wrapper ... caller=010044b2`).
  - `trace_scene_runtime_tick` continues after `loadFlags=0,0,0`, but still shows `pendingResync=1`.
- source update:
  - added `trace_progress_strip_wrapper_tick` so the next run can prove whether the center strip is only an initialization-frame draw or continues in the stable scene after `loadFlags=0,0,0`.
  - restored a narrow `0x0100F468` portal move-entry append guard that skips only the confirmed `.sce` portal-shaped candidate (`actorId=1`, `grid=223,382`, `box=203,402,240,422`, `kind=2`) before `entryCount` is incremented.
  - did not restore any host-side active state advancement and did not patch draw functions.
- validation:
  - `make` succeeds after the source/doc updates.

## 2026-06-08 latest rerun: portal entry blocked, progress trace corrected

- verified from the newest `bin/logs/net_packets.log`: the latest session reaches `builtin-group-type1`, `builtin-game-type` 2/3, and `builtin-scene-resource-followup`; there is no newer unhandled packet after that session's follow-up response.
- verified from `bin/logs/net_trace.log`: the narrow `0x0100F468` portal append guard fired on the current run (`trace_portal_move_entry_append_skipped ... actorId=1 grid=223,382 box=203,402,240,422 kind=2`), and the move-entry table immediately reports `count=0`.
- verified: after that skip in the latest session, there is no new `trace_actor_move_entry_append` or `trace_scene_body_draw_dispatch`; the protagonist remains on the normal current-node path (`trace_scene_current_node_draw ... actorId=10001 ... visualRes=054045a8 grid=223,382 screen=103,65`). If fragments are still visible, they need a separate draw-owner trace because the known portal/body-entry source is gone.
- corrected: the repeated settled-scene `trace_progress_strip_wrapper_tick ... caller=01005a68 dst=0,67` was a false attribution from an over-broad region filter. IDA maps `0x01005A0C/0x01005A68` to `DF_PictureLibrary.c` picture-library drawing, not the center progress strip. The real loading.gif/widget path at `0x010460CA` appears once around `tick=128`, and the center strip tiler `caller=010044b2` belongs to the earlier bootstrap batch.
- corrected: `trace_scene_list_return ... packet=00000000` for `30/7` is not proof of empty `rolesinfo` by itself; IDA shows `sub_1039430()` returns `0` normally. The role `colnames` stream is confirmed parseable, while direct `rolesinfo` item parsing remains unconfirmed.
- changed: narrowed `trace_progress_strip_wrapper_tick` in `src/main.c` so future runs only report the center strip caller `0x010044B2` or the real loading.gif widget caller `0x010460CA`, avoiding the noisy `0x01005A68` map/background path.

## 2026-06-08 later rerun: steady scene loading.gif owner still active

- corrected from the latest manual run: after the `0x01005A68` picture-library noise was filtered out, the real scene `loading.gif` widget is still being drawn every tick. `bin/logs/net_trace.log` shows `trace_progress_strip_wrapper_tick ... caller=010460ca ... dst=20,188` from `tick=401` through the tail; `tick=402+` already has `loadFlags=0,0,0`.
- verified network side: `bin/logs/net_packets.log` ends the session with the accepted `builtin-scene-resource-followup` response and no newer WT request or unhandled packet. Current evidence still points to local scene/HUD scheduling, not an immediately missing server response.
- verified fragment side: the portal-derived body entry remains blocked in the same run (`trace_portal_move_entry_append_skipped` followed by `trace_actor_move_entry_table ... count=0`), and there are no `trace_actor_move_entry_append` / `trace_scene_body_draw_dispatch` hits after that. The protagonist current-node draw path remains alive.
- static correction: `loading_gif_widget_draw()` (`0x010461A8`) always calls `loading_gif_widget_draw_frame()` (`0x010461CA`), even when the global gate byte at `R9+0x5530` is clear. The gate toggles `widget+0x10` / frame reset behavior; it is not itself the owner-level hide condition.
- changed: added a read-only `trace_loading_gif_widget_draw_entry` hook at `0x010461A8` for the scene widget rooted at `Global_R9+0x60F4`. The next run will log high-level LR/caller plus `widget+0x10`, `R9+0x5530`, frame counter, image index, and mode so the remaining owner can be identified without suppressing or patching drawing.

## 2026-06-08 latest rerun: loading widget owner is sub_1013C8A

- verified: the new owner trace resolves the stable scene loading widget path. `bin/logs/net_trace.log` shows repeated `trace_loading_gif_widget_draw_entry ... pc=010461a8 lr=01013cbb caller=01013cba ... widget=01056cc4 args=20,188,200`, so the high-level owner is `sub_1013C8A()`.
- verified by static IDA: `sub_1013C8A()` calls the scene widget callback at `0x01013CB8` only if `s16[R9+0x9590] <= 0` and the scene gate bytes `R9+0x5C67` / `R9+0x5C68` are both nonzero. `scene_runtime_tick()` calls `sub_1013C8A()` unconditionally in the late draw section at `0x010157D4`, immediately after `sub_1013D46()`.
- corrected: the late persistent bar no longer depends on `R9+0x5530`. Early in the run, `gate5530=1` and `frame` increments; later the same trace shows `flag10=0`, `gate5530=0`, `frame=0`, but `sub_1013C8A()` still dispatches the widget every tick.
- unchanged: the packet tail still has no newer request after `builtin-scene-resource-followup`, and the portal/body-entry path remains blocked by the narrow guard.
- changed: extended the read-only `trace_loading_gif_widget_draw_entry` output to include `sceneGate=R9+0x5C67/5C68` and `overlayCounter=s16[R9+0x9590]`, matching the exact `sub_1013C8A()` branch conditions for the next manual run.

## 2026-06-08 newest rerun: owner conditions confirmed, fragment source still open

- verified from `bin/logs/net_packets.log`: the latest session still completes through `builtin-scene-resource-followup` (`WT len=49`, response objects `1/12/1,1/7/42,1/6/1,1/6/13,1/6/14,1/2/10,1/25/5`) and no newer WT request appears after that.
- verified from `bin/logs/net_trace.log`: `sub_1013C8A()` continues to dispatch the scene `loading.gif` widget with `sceneGate=1,1` and `overlayCounter=0` (`trace_loading_gif_widget_draw_entry ... caller=01013cba ... args=20,188,200`), so the visible bar is not an unserviced immediate network wait.
- corrected by static IDA: the value traced as `overlayCounter` at `R9+0x9590` is the `R9+0x9588` manager's queued-entry count. `sub_10366AC()` appends a manager node and increments `*(u16 *)(R9+0x9588+8)` at `0x01036700`; `sub_1013C8A()` draws the scene widget when that count is `<= 0` and the two scene gate bytes are set.
- verified fragment-side: the narrow portal move-entry guard is active in this run (`trace_portal_move_entry_append_skipped ... actorId=1 grid=223,382 box=203,402,240,422 kind=2`), then `trace_actor_move_entry_table ... count=0`; no later `trace_actor_move_entry_append` or `trace_scene_body_draw_dispatch` appears.
- changed: added a read-only `trace_scene_actor_region_draw` in `src/vmFunc.c` for `vMDrawImageWithClipEx()` and `vMDrawImageClipAndAlphaEx()`. It only logs settled-scene image blits whose final destination overlaps the main actor/loading region, so the next manual run can identify the remaining actor-adjacent fragments by image API, source dimensions, destination rectangle, LR/caller, and `lastAddress`.

## 2026-06-08 latest rerun: fragments are tiled progress-panel skin

- verified from the newest manual run: `trace_scene_actor_region_draw` hit 160 samples, all drawing the same `36x36` image object (`srcInfo=05016cd0`, `srcPtr=05019d98`) with a `12x12` center tile (`sx=12 sy=12 w=12 h=12`) across the actor/loading region.
- evidence: representative trace lines show destinations stepping by 12 pixels (`dx=48,60,72...`, `dy=192,204...`) and return path `lr=01004141 caller=01004175 last=0100413e`.
- static confirmation: IDA maps `0x0100413E/0x01004150/0x01004174` to `sub_1004096()` / `sub_1004150()`, and maps the higher `caller=010044b2` path to `sub_1004362()`, the 36x36 nine-slice/panel tiler.
- corrected: the remaining visible fragments are not currently explained by the old protagonist body/portal entry. The portal-shaped move-entry candidate is still skipped (`trace_portal_move_entry_append_skipped`) and the table reports `count=0`; there is still no later `trace_scene_body_draw_dispatch`.
- verified: the scene widget owner remains active through `sub_1013C8A()` with `sceneGate=1,1` and manager count `R9+0x9590 == 0`; `trace_loading_gif_widget_draw_entry` continues every tick with `args=20,188,200`.
- static map-title note: `sub_100F094()` writes the top-center scene/status text only in the `actorTag == 4` branch when property kind `22` is read (`0x0100F68A -> R9+0x5CD8+4`). The current logged `.sce` portal collision starts on portal grammar and only the `actorTag == 2` candidate reaches the move-entry append guard.
- hypothesis: the disappeared top-center map name means the current scene-key descriptor path is not delivering a valid `actorTag=4/property=22` status record before/after the portal-tail collision. Treat this separately from `actorinfo` and `1/2/10 otherinfo`, which remain matched to their confirmed parsers.
- next: trace or reconstruct the valid scene descriptor/status record contract that should feed `sub_100F094()` with `actorTag=4/property=22` and should naturally stop scheduling the loading/progress widget, rather than suppressing `sub_1013C8A()` or drawing calls.

## 2026-06-08 trace update: scene status record visibility

- changed: extended the existing read-only `parse_actor_motion_descriptor()` handoff trace to dump 48 tail bytes / 24 little-endian shorts at the exact callback cursor. The next manual run should show whether a valid `sub_100F094()` actor-record stream, including a possible `actorTag=4`, exists after the currently observed portal-token prefix.
- changed: added read-only hooks at the two confirmed `sub_100F094()` scene-text write sites:
  - `0x0100F6A0`: property kind `0x12` -> `R9+0x5C64+0x74` (`R9+0x5CD8`, prompt/action descriptor name)
  - `0x0100F68A`: property kind `0x16` -> `R9+0x5C64+0x78` (`R9+0x5CDC`, centered top status/map text)
- changed: extended the `0x0100F78E` move-entry table snapshot to include the current `R9+0x5CD8` and `R9+0x5CDC` pointers and decoded labels.
- validation: `make` succeeds after these trace-only changes.
- scope: this is instrumentation only. It does not change mock packet builders, guest globals, parser return values, draw calls, or the loading-widget owner.

## 2026-06-08 rerun: status text record is present, follow-up dispatch is the next suspect

- verified from the newest `bin/logs/net_trace.log`: the extended `trace_actor_motion_callback_handoff` tail begins with the known portal tokens, but continues into additional scene descriptor tokens. The later parser path reaches both confirmed `sub_100F094()` scene-text write sites.
- verified: `trace_scene_status_text_write field=promptName_0x12_R9_5CD8 ... newText=<empty>` fires at `0x0100F6A0`, and `trace_scene_status_text_write field=statusText_0x16_R9_5CDC ... newText=蓬莱-铜雀台` fires at `0x0100F68A`.
- corrected: the hypothesis that the current descriptor stream never delivers `actorTag=4/property=22` is now disproven for this run. The top-center status/map text is parsed and stored into `R9+0x5CDC`.
- still verified: the portal-shaped move-entry candidate is skipped, and the final `trace_actor_move_entry_table` reports `count=0` with `statusText=蓬莱-铜雀台`.
- still verified: `sub_1013C8A()` keeps drawing the scene loading widget with `sceneGate=1,1` and manager queue count `R9+0x9590 == 0`; `trace_progress_strip_wrapper_tick` remains on the `loading.gif` path (`caller=0x010460CA`, dst `20,188`).
- new dispatch clue:
  - the scene-resource follow-up response is queued and fired (`mock_response ptr=050179a0 len=319`, then `fire_event ... r0=050179a0 tick=138`), but current handler traces do not show its `12/1`, `7/42`, `6/*`, `2/10`, or `25/5` objects entering `net_business_response_dispatch()` handlers.
  - the only visible business handler group in this window remains the earlier group-type response at tick 136 (`kind=10/subtype=26`, `30/1`, `30/3`, `30/7`).
- changed: added read-only `trace_business_dispatch_state` / `trace_business_dispatch_item` hooks on `net_business_response_dispatch()` (`0x01012E4C`, `0x01012E88`, `0x01012E9E`, and early-exit sites) so the next manual run can determine whether the scene-resource follow-up packet is blocked by the dispatch gate, unpacked into an unexpected object table, or bypassing this dispatcher entirely.
- validation: `make` succeeds after the new dispatch instrumentation.

## 2026-06-08 rerun: scene-resource follow-up is blocked by dispatch gate

- verified from the newest run:
  - the initial group/type response enters `net_business_response_dispatch()` with `dispatchGate=1`, unpacks 5 objects, and dispatches `kind/subtype` pairs `5/10`, `10/26`, `30/1`, `30/3`, and `30/7`.
  - after that same dispatch returns through `fallback_exit`, `trace_business_dispatch_state` reports `dispatchGate=0`.
  - the subsequent `7/7 type=2` and `7/7 type=3` packets enter the dispatcher with `dispatchGate=0` and take the `early_gate_off` path before object dispatch.
  - the later `builtin-scene-resource-followup` packet also enters the dispatcher (`r0=050179a0`, len `0x13f`, `objectCount=7`) but immediately takes `early_gate_off`; none of its `12/1`, `7/42`, `6/*`, `2/10`, or `25/5` objects reach per-item dispatch.
- evidence:
  - `trace_business_dispatch_state label=entry ... r0=050179a0 ... dispatchGate=0 objectCount=7`
  - `trace_business_dispatch_state label=early_gate_off ... r0=00000000 ...`
  - persistent `trace_loading_gif_widget_draw_entry ... sceneGate=1,1 overlayCounter=0`
- implication:
  - the mock response bytes are no longer the immediate bottleneck for this follow-up; the client refuses to process the follow-up because the scene object's business-dispatch gate byte at `sceneObj+0x164` is already clear.
  - the next target is the writer that clears `sceneObj+0x164` after the first scene dispatch. Do not force the gate open; identify whether a packet ordering/response bundling expectation is being violated.
- changed: added read-only `trace_scene_dispatch_gate_write` from the memory-write hook. It logs writes covering the current `sceneObj+0x164` gate slot with old/new value, write address, PC/LR, `lastAddress`, and tick.
- validation: `make` succeeds after the gate-write instrumentation.

## 2026-06-08 rerun: 30/1 scene parser closes the dispatch window

- verified from the newest manual run:
  - `trace_scene_dispatch_gate_write ... pc=0508338c ... old=0 new=1 ... activeScreen=01053450` opens the scene business-dispatch gate just before the first scene-enter response is handled.
  - `net_business_response_dispatch()` then enters with `dispatchGate=1`, unpacks 5 objects, and dispatches `5/10`, `10/26`, `30/1`, `30/3`, and `30/7`.
  - while item `30/1` is being handled, `trace_scene_dispatch_gate_write ... pc=01039766 ... old=1 new=0` clears `sceneObj+0x164`.
  - the later `7/7 type=2`, `7/7 type=3`, and `builtin-scene-resource-followup` responses all enter the dispatcher with `dispatchGate=0` and take `early_gate_off`; the follow-up packet's `objectCount=7` is visible but none of its objects are dispatched.
- static IDA evidence:
  - `net_business_response_dispatch()` checks `sceneObj+0x164` at `0x01012E6E..0x01012E72` before `event_packet_init()`.
  - `parse_scene_response_entry()` / the `30/1` scene handler reads `scene` and `posinfo`, calls scene-object methods, then clears `sceneObj+0x164` at `0x01039766`.
- corrected implication:
  - this run strengthens the protocol-side explanation for the persistent loading/progress panel. The follow-up data is not merely a local residue after a successfully consumed response; it is delivered after the client has already closed this dispatch window.
- changed:
  - `vm_net_mock_build_group_type1_response()` now appends the same seven scene-resource follow-up objects (`12/1`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10`, `25/5`) after the existing `30/1`, `30/3`, and `30/7` objects, so they are delivered inside the still-open first dispatch call without forcing the gate open.
  - `vm_net_mock_build_scene_resource_followup_response()` now reuses the same helper to keep the standalone follow-up bytes aligned; the later standalone response may still be requested and ignored by the closed gate, but it should no longer be the only copy.
- hypothesis to verify on the next manual run:
  - the first `trace_business_dispatch_state label=after_unpack_ok` for `builtin-group-type1` should report 12 objects, and `trace_business_dispatch_item` should show the bundled `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10`, and `25/5` entries after `30/7`.
  - if those objects dispatch successfully, check whether `sceneGate=1,1` / `sub_1013C8A()` loading-widget draws and the actor-adjacent tiled panel fragments disappear or change.
- validation:
  - `make` succeeds after the builder change.

## 2026-06-08 rerun: oversized bundled follow-up is rejected

- verified from the next manual run:
  - the experimental `group-type1` response grew to `responseLen=781` and `mock_group_type1_response ... objects=12`.
  - `net_business_response_dispatch()` entered with `dispatchGate=1`, but `event_packet_init()` failed before per-object dispatch:
    - `trace_business_dispatch_state label=entry ... r1=0000030d ... dispatchGate=1`
    - `trace_business_dispatch_state label=unpack_error ... r0=00000003 ... objectCount=10`
  - because `30/1` did not run in that failed first response, `sceneObj+0x164` stayed open and the later standalone `builtin-scene-resource-followup` was no longer blocked by the gate.
  - that standalone follow-up dispatched `12/1`, `7/42`, `6/1`, and `6/13`, then crashed before item 4:
    - stdout fault: `pc=0000000a`, `lr=01012e9f`, `lastPc=01012e9c`, `r1=05001560`, `r5=4`, `r7=050179a0`
    - evidence points to the per-item pre-dispatch callback at `0x01012E9C` trying to `BLX 0xA` after `6/13` processing.
- implications:
  - a single 12-object bundled response is not a valid shape for this dispatcher/window. The current unpack path appears to have a practical table/count limit around 10 objects for this response family.
  - `6/13 tasktypes` / `6/14 task action` are not safe to force through the scene business dispatcher in this state; keep them as separate parser-contract questions.
- changed:
  - narrowed the group/type1 bundling experiment to five core follow-up objects only: `12/1`, `7/42`, `6/1`, `2/10`, and `25/5`.
  - this keeps the first response at 10 total objects (`5/10`, `10/26`, `30/1`, `30/3`, `30/7`, plus the five core follow-up objects), avoiding the observed unpack limit and excluding the `6/13` / `6/14` crash path.
  - the standalone `vm_net_mock_build_scene_resource_followup_response()` still emits the full 7-object body for comparison, but on the normal path it should again be blocked after `30/1` closes the gate.
- validation:
  - `make` succeeds after narrowing the bundled object set.

## 2026-06-08 rerun: 10-object bundle fits, but same-window 2/10 corrupts node draw callback

- verified from the next manual run:
  - the narrowed first response is accepted: `mock_group_type1_response ... objects=10 ... len=675`, followed by `trace_business_dispatch_state label=after_unpack_ok ... objectCount=10`.
  - all ten objects dispatch in order:
    - `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `12/1`, `7/42`, `6/1`, `2/10`, `25/5`.
  - `30/1` still naturally clears `sceneObj+0x164` at `0x01039766`, and the same in-progress dispatch continues through the later bundled objects. Later `7/7 type=2/3` responses are then correctly blocked by `early_gate_off`.
- new negative result:
  - after the bundled `2/10`, the next actor draw crashes through a corrupted current-node draw callback:
    - stdout: `pc=9883a110`, `lr=01014597`, `lastPc=01014594`
    - static: `0x01014594` is `scene_draw_actor_pass()` loading and calling `ActorSceneNode.drawAt` from node offset `+0x148`.
  - before this same scene-enter dispatch, the current node was published with sane callbacks:
    - `trace_scene_current_node_publish ... current=05400000 ... drawAt=01045579 step=01045429`
  - therefore same-window `1/2/10 otherinfo` is not confirmed-safe. It likely updates or recreates the already-published actor node in a way that corrupts callback-bearing node fields, or our `otherinfo` tuple is still incomplete for this later scene state.
- changed:
  - removed `2/10` from the bundled group/type1 core follow-up set.
  - the first response now bundles only `12/1`, `7/42`, `6/1`, and `25/5`, for 9 total objects.
  - the standalone scene-resource follow-up still emits full `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10`, `25/5` for comparison, but the normal path should block it after `30/1` closes the gate.
- validation:
  - `make` succeeds after removing same-window `2/10` from the bundled response.

## 2026-06-08 rerun: 9-object bundle is stable but loading remains

- verified from the newest manual run:
  - `mock_group_type1_response ... objects=9 ... len=579`.
  - `net_business_response_dispatch()` accepts the response: `trace_business_dispatch_state label=after_unpack_ok ... objectCount=9`.
  - dispatch order is `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `12/1`, `7/42`, `6/1`, `25/5`.
  - `30/1` still clears `sceneObj+0x164` at `0x01039766`; the same in-progress dispatch continues through the bundled objects.
  - later `7/7 type=2`, `7/7 type=3`, and standalone `builtin-scene-resource-followup` still enter with `dispatchGate=0` and take `early_gate_off`.
- verified stable actor state:
  - stdout has no invalid-PC fault in this run.
  - current actor node callbacks remain valid through draw: `trace_scene_current_node_draw ... actorId=10001 ... callback=01045579 drawAt=01045579 step=01045429`.
  - HP/SP display values are still sane: `trace_status_bar_divide_site ... current=120 baseMax=120 displayMax=120` and `current=100 baseMax=100 displayMax=100`.
- still unresolved:
  - `trace_loading_gif_widget_draw_entry` continues from `sub_1013C8A()` with `sceneGate=1,1` and `overlayCounter=0`, even after `runtime_state ... loadFlags=0,0,0`.
  - therefore the stable 9-object bundle avoids the `2/10` callback corruption but is not enough to clear the loading/progress owner.
- changed:
  - added read-only write tracing for the three `sub_1013C8A()` owner inputs: `R9+0x5C67`, `R9+0x5C68`, and `R9+0x9590`.
  - this does not force guest state or patch game logic; it only records old/new values plus PC/LR/last/tick for the next run.
- hypothesis:
  - the remaining protocol gap is likely either the correct safe timing/shape for omitted `6/13` / `6/14`, or another scene-local queue/gate contract that should clear one of the `sub_1013C8A()` branch inputs.

## 2026-06-08 rerun: loading owner gate writer identified

- verified from the newest manual run:
  - the only `trace_scene_loading_owner_write` hits are at tick 192:
    - `pc=010132c8` writes `R9+0x5C67` from `0` to `1`
    - `pc=010132ca` writes `R9+0x5C68` from `0` to `1`
  - no write to `R9+0x9590` appears, and no later write clears either scene loading-owner gate byte.
  - the loading widget remains active through the tail: `trace_loading_gif_widget_draw_entry ... sceneGate=1,1 overlayCounter=0`.
- static IDA evidence:
  - `0x010132C8/0x010132CA` are in `scene_runtime_init_and_sync()`'s fresh scene-enter path and unconditionally set `R9+0x5C67/5C68` to `1`.
  - `sub_1013C8A()` draws `R9+0x60F4` when `s16[R9+0x9590] <= 0` and both gate bytes are nonzero.
  - `net_handle_business_followup_events()` contains a protocol-side gate writer: `kind=27/subtype=11` writes `R9+0x5C67` based on the scene object's `fb` byte; older notes and IDA show `kind=27/subtype=12` is the primary handler that stores `fb=1`.
- changed:
  - the bundled group/type1 follow-up experiment now replaces the unconfirmed `25/5` object with a `27/12 + 27/11` pair, keeping the first response at 10 objects total.
  - the bundled set is now `12/1`, `7/42`, `6/1`, `27/12`, `27/11`. It still excludes the known-unsafe same-window `2/10` and the `6/13` / `6/14` task-list pair.
- hypothesis to verify:
  - the next run should show `trace_business_handler ... kind=27 subtype=12`, then `kind=27 subtype=11`, followed by `trace_followup_case label=case27_11` and a `trace_scene_loading_owner_write` clearing `R9+0x5C67` to `0`.
  - if only gateA clears, `sub_1013C8A()` should stop drawing because it requires both `R9+0x5C67` and `R9+0x5C68` to be nonzero.

## 2026-06-08 rerun: 27/12 + 27/11 clears the scene loading widget owner

- verified from the newest manual run:
  - `mock_group_type1_response ... objects=10 ... bundledFbTargetClear=1 len=634`.
  - `net_business_response_dispatch()` accepts the first-window response: `trace_business_dispatch_state label=after_unpack_ok ... objectCount=10`.
  - dispatch order reaches `27/12` and `27/11` in the same call after `30/1` has already cleared `sceneObj+0x164`:
    - `trace_business_dispatch_item ... index=8 ... kind=27 subtype=12`
    - `trace_business_handler pc=01010bc0 ... kind=27 subtype=12`
    - `trace_business_dispatch_item ... index=9 ... kind=27 subtype=11`
    - `trace_business_handler pc=01010bc0 ... kind=27 subtype=11`
  - the follow-up scanner then reaches `case27_11` and naturally clears the loading-widget owner gate:
    - `trace_followup_case label=case27_11 pc=010106d8 ...`
    - `trace_scene_loading_owner_write field=sceneGateA_R9_5C67 ... old=1 new=0 ... pc=010106f4`
  - there are zero `trace_loading_gif_widget_draw_entry` hits and zero `trace_progress_strip_wrapper_tick` hits in this run.
- confirmed packet sequencing change:
  - the old separate `WT len=49` scene-resource follow-up request is gone.
  - the later request is only `WT len=14` with `1/12/1,1/7/42`, answered by `builtin-login-tail-skill`.
  - later `7/7 type=2`, `7/7 type=3`, and the small tail packet enter with `dispatchGate=0` and take `early_gate_off`, as expected after `30/1`.
- remaining issue:
  - the trace no longer supports "the visible strip is still `sub_1013C8A()` / scene `loading.gif`".
  - the tail repeatedly logs `trace_loading_overlay_call label=sub_1003568 ... sceneTickGate=0,1`, so any remaining actor-adjacent fragments need to be attributed to the gateA-cleared overlay/UI path, not the old loading.gif owner.
- changed:
  - adjusted the read-only actor-region draw trace so it records after `sceneGate=0,1` instead of requiring the older `1,1` settled predicate. The previous trace cap was exhausted before `27/11` cleared gateA, so it could not prove the post-clear fragment source.
- validation:
  - `make` succeeds after the trace-only adjustment.

## 2026-06-08 rerun: 27/11 alone leaves scene tick in loading-overlay branch

- verified from the newest manual run:
  - the 10-object first-window bundle is still accepted and consumed:
    - `trace_business_dispatch_state label=after_unpack_ok ... objectCount=10`
    - `trace_business_dispatch_item ... index=8 ... kind=27 subtype=12`
    - `trace_business_dispatch_item ... index=9 ... kind=27 subtype=11`
    - `trace_followup_case label=case27_11 ...`
  - `case27_11` still clears `R9+0x5C67`: `trace_scene_loading_owner_write field=sceneGateA_R9_5C67 ... old=1 new=0 ... pc=010106f4`.
  - the former scene `loading.gif` owner remains stopped: `trace_loading_gif_widget_draw_entry count=0` and `trace_progress_strip_wrapper_tick count=0`.
- new negative/partial result:
  - immediately after `R9+0x5C67` is cleared, `scene_runtime_tick()` enters `sub_1003568 -> sub_100337C(0)` every frame with `sceneTickGate=0,1`.
  - the post-gate actor-region trace confirms the visible fragments are live overlay draws, not stale pixels:
    - `trace_loading_overlay_call label=sub_1003568 ... lr=01014d85 ... sceneTickGate=0,1`
    - `trace_scene_actor_region_draw ... srcDim=36,36 ... sx=12 sy=12 w=12 h=12 ... sceneGate=0,1 loadFlags=1,0,0`
    - the tiles cover a 12-pixel grid around the actor/loading region (`dx=48..180`, `dy=192..348`).
- static evidence:
  - `scene_runtime_tick()` checks `R9+0x5C67/0x5C68` at `0x01014D74..0x01014D80`; if either byte is zero, it calls `sub_1003568()`.
  - `net_handle_business_followup_events()` case `27/4` is the matching ready/finalize branch: when `result==1` and `type==1`, it writes `R9+0x5C67=1` at `0x0101087E` and `R9+0x5C68=1` at `0x01010882`.
- changed:
  - adjusted the first-window bundle experiment to keep the 10-object limit but replace empty `6/1 taskinfo` with `27/4(result=1,type=1,fb=1,info="")`.
  - the bundled follow-up set is now `12/1`, `7/42`, `27/12`, `27/11`, `27/4`.
- validation:
  - `make` succeeds after the builder change.
- next verification:
  - the next run should show `trace_followup_case label=case27_4` after `case27_11`, followed by owner writes returning `R9+0x5C67/0x5C68` to `1,1`.
  - if `sub_1003568` still repeats afterward, the remaining issue is no longer the fb-target gate pair and should move to `loadFlags=1,0,0` / scene resource completion.

## 2026-06-08 rerun: 27/4 works, but 6/1 must stay bundled

- verified from the newest manual run:
  - the rebuilt first-window response is accepted with `mock_group_type1_response ... objects=10 ... len=669`.
  - dispatch order includes the new ready/finalize object:
    - `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `12/1`, `7/42`, `27/12`, `27/11`, `27/4`.
  - follow-up scanner reaches `case27_4` after `case27_11`.
  - writes are exactly as static IDA predicted:
    - `case27_11`: `R9+0x5C67` `1 -> 0` at `0x010106F4`
    - `case27_4`: `R9+0x5C67` `0 -> 1` at `0x0101087E`
    - `case27_4`: `R9+0x5C68` `1 -> 1` at `0x01010882`
- corrected:
  - `27/4` fixes the `sceneTickGate=0,1` / `sub_1003568` tail created by `27/11` alone, but it is not sufficient to remove the loading widget.
  - after `27/4`, `sub_1013C8A()` is again true with `sceneGate=1,1` and `overlayCounter=0`; the real `loading.gif` path redraws every tick through `caller=01013cba` / `010460ca`.
- new packet-diff evidence:
  - removing bundled `6/1 taskinfo` to make room for `27/4` caused the old large `WT len=49` scene-resource follow-up request to return.
  - that standalone response is still blocked by `dispatchGate=0`, so this regression is real protocol sequencing evidence, not a local visual artifact.
- changed:
  - adjusted the first-window bundle again: keep `6/1 taskinfo`, keep `27/12`, `27/11`, and `27/4`, and omit the lower-priority `12/1 + 7/42` pair from this first window.
  - the new group/type1 response should be 9 objects total: base `5/10`, `10/26`, `30/1`, `30/3`, `30/7` plus `6/1`, `27/12`, `27/11`, `27/4`.
- validation:
  - `make` succeeds after the builder change.
- next verification:
  - rerun and check whether the large `WT len=49` request disappears while the small `12/1 + 7/42` request remains.
  - if `loading.gif` still persists with `sceneGate=1,1 overlayCounter=0`, the next target is the `R9+0x9588` manager-count producer/consumer contract, not the fb-target trio.

## 2026-06-08 rerun: 6/1 plus fb-target trio still misses skill/book pair

- verified from the newest manual run:
  - the 9-object first-window bundle is accepted: `mock_group_type1_response ... objects=9 ... len=613`.
  - dispatch order is `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `6/1`, `27/12`, `27/11`, `27/4`.
  - `case27_4` still executes after `case27_11` and restores `R9+0x5C67/5C68` to `1,1`.
- negative result:
  - the large `WT len=49` follow-up request still appears and is still answered by `builtin-scene-resource-followup`.
  - that standalone response still enters `net_business_response_dispatch()` with `dispatchGate=0` and takes `early_gate_off`.
  - because this run retained `6/1`, the missing first-window objects implicated by this specific diff are now `12/1 + 7/42`.
- loading status:
  - `sub_1003568` is not the tail owner after `27/4`; the tail is again `sub_1013C8A()` / `loading.gif` with `sceneGate=1,1 overlayCounter=0`.
  - `loadFlags` eventually reaches `0,0,0`, so this remains a widget-owner / `R9+0x9588` manager-count question after the follow-up sequencing is stabilized.
- changed:
  - adjusted the bundled first-window follow-up set to `12/1`, `7/42`, `6/1`, and `27/4`.
  - this keeps the response at 9 objects total and drops `27/12 + 27/11`, since `27/11` is already confirmed to create the `sceneTickGate=0,1` waiting branch and both objects consume scarce first-window slots.
- validation:
  - `make` succeeds after the builder change.
- next verification:
  - rerun and check whether the large `WT len=49` request disappears.
  - check whether `case27_4` alone still fires and whether the `loading.gif` owner remains at `sceneGate=1,1 overlayCounter=0`.

## 2026-06-08 rerun: skill/book plus 6/1 plus 27/4 still misses more follow-up state

- verified from the newest manual run:
  - `mock_group_type1_response ... objects=9 ... len=615` is accepted and dispatches `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `12/1`, `7/42`, `6/1`, and `27/4`.
  - `case12_1` and `case27_4` execute; `27/4` writes the scene loading-owner gate pair to `1,1`.
  - the large `WT len=49` follow-up request still appears at tick 324 and asks for `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10`, and `25/5`.
  - the standalone response is still blocked by `dispatchGate=0`, so the late `6/13`, `6/14`, `2/10`, and `25/5` entries are not consumed.
  - the visible progress/fragments remain the `sub_1013C8A()` / `loading.gif` path with `sceneGate=1,1 overlayCounter=0`.
- superseded experiment:
  - the temporary first-window `6/13`/`6/14` experiment is superseded by the next entry. It confirmed a hard mismatch rather than progress.

## 2026-06-08 rerun: same-window 6/13 corrupts the dispatch callback

- verified from the newest manual run:
  - the 10-object packet is accepted by `event_packet_init()`: `trace_business_dispatch_state label=after_unpack_ok ... objectCount=10`.
  - dispatch reaches item index 6, `kind=6 subtype=13`.
  - immediately after `6/13`, the next item dispatch crashes through a corrupted per-item callback:
    - stdout: `pc=04710400`, `lr=01012e9f`, `lastPc=01012e9c`, `r5=7`.
    - trace: `trace_scene_loading_owner_write ... pc=0104d43c/0104d410` writes garbage-looking values into the `R9+0x5C67/0x5C68` region.
- static evidence:
  - IDA maps `0x0104D410` / `0x0104D43C` to memcpy internals.
  - IDA maps `net_handle_task_response_dispatch()` case `13` to `0x01047896`. The parser reads `tasktypes`, loops six slots (`R4=2..7`), reads a short type id, clears a 10-byte name slot, then reads a length/pointer pair and memcpy's that many bytes into the slot.
- corrected:
  - the current `vm_net_mock_append_tasktypes_empty13_object()` uses the generic `vm_net_mock_seq_put_string()` helper for the inner name, but this does not match the case-13 stream reader in the live scene-enter dispatcher state.
  - `6/13` and `6/14` are no longer candidates for first-window bundling until the exact tasktypes blob encoding is recovered.
- changed:
  - restored `30/3` and `30/7` by default and removed `6/13/6/14` from the bundled first-window set.
  - new bundled order is `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `12/1`, `7/42`, `6/1`, `25/5`, `27/4`.
  - same-window `2/10` remains excluded because the earlier accepted `2/10` run corrupted the actor draw callback.
- next verification:
  - rerun and check whether adding safe `25/5` suppresses or changes the large `WT len=49` request.
  - if WT49 persists, the remaining confirmed-unsafe gaps are `6/13/6/14` tasktypes/taskaction and `2/10 otherinfo`, which need parser-level field recovery before more bundling attempts.

## 2026-06-08 rerun: same-window 25/5 does not suppress WT49

- verified from the newest manual run:
  - the 10-object first-window packet is accepted and stable: `mock_group_type1_response ... objects=10 ... len=633`.
  - dispatch order reaches `25/5` at index 8 and `27/4` at index 9 after the base scene entries and `12/1`, `7/42`, `6/1`.
  - `case12_1` and `case27_4` execute; `27/4` writes `R9+0x5C67/0x5C68` as `1 -> 1`.
  - no stdout fault occurs in this run.
- negative result:
  - the client still sends the large `WT len=49` follow-up at tick 134.
  - the standalone response is still a late `builtin-scene-resource-followup`; because it is delivered after `30/1` has closed the scene business-dispatch gate, it is not the right way to satisfy the parser on the normal path.
  - the scene `loading.gif` owner persists with `sceneGate=1,1 overlayCounter=0`.
- changed:
  - replaced the ineffective first-window `25/5` slot with `27/11`, followed by `27/4`.
  - new bundled order is `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `12/1`, `7/42`, `6/1`, `27/11`, `27/4`.
- next verification:
  - rerun and check whether `27/11 + 27/4` suppresses the large `WT len=49` request.
  - verify that `case27_11` and `case27_4` both run, and that the final scene tick gate is restored to `1,1`.

## 2026-06-08 rerun: 27/11 plus 27/4 still does not suppress WT49

- verified from the newest manual run:
  - the first-window packet is accepted with `objects=10 len=621`.
  - dispatch order is `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `12/1`, `7/42`, `6/1`, `27/11`, `27/4`.
  - `case27_11` and `case27_4` both execute.
  - because no `27/12` preceded it, `case27_11` leaves `R9+0x5C67` at `1 -> 1`; `27/4` leaves the final gate pair at `1,1`.
- negative result:
  - the client still sends the large `WT len=49` request at tick 150.
  - the scene `loading.gif` widget remains active through `sub_1013C8A()`.
- changed:
  - added separate `CBE_GROUP_TYPE1_ROOM_NPC` and `CBE_GROUP_TYPE1_ROOM_ROLES` switches.
  - default now keeps `30/3` room NPC and omits `30/7` room roles to free one first-window slot.
  - new bundled order is `5/10`, `10/26`, `30/1`, `30/3`, `12/1`, `7/42`, `6/1`, `27/12`, `27/11`, `27/4`.
- next verification:
  - rerun and check whether the full `27/12 + 27/11 + 27/4` trio suppresses `WT len=49` while keeping `R9+0x5C67/0x5C68` restored to `1,1`.
  - watch for any new symptom from omitting `30/7` room roles.

## 2026-06-08 rerun: full fb-target trio with 30/3 but without 30/7 still does not suppress WT49

- verified from the newest manual run:
  - `mock_group_type1_response ... objects=10 ... sceneRoomNpc=1 sceneRoomRoles=0 ... len=558`.
  - `net_business_response_dispatch()` accepts the packet: `trace_business_dispatch_state label=after_unpack_ok ... objectCount=10`.
  - dispatch order is `5/10`, `10/26`, `30/1`, `30/3`, `12/1`, `7/42`, `6/1`, `27/12`, `27/11`, `27/4`.
  - `27/12` reaches the primary `net_handle_fb_target_dispatch()` path at `0x01010BC0`.
  - the follow-up scanner reaches both `case27_11` and `case27_4`.
  - `case27_11` clears `R9+0x5C67` from `1 -> 0` at `0x010106F4`; `case27_4` restores `R9+0x5C67` from `0 -> 1` at `0x0101087E` and writes `R9+0x5C68` at `0x01010882`.
- negative result:
  - the client still sends the large `WT len=49` request at tick 591 for `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10`, and `25/5`.
  - the standalone `builtin-scene-resource-followup` response then enters `net_business_response_dispatch()` at tick 592 with `dispatchGate=0` and exits via `early_gate_off`, so those seven objects are still not consumed in the normal path.
  - the tail still shows `trace_loading_gif_widget_draw_entry ... caller=01013cba ... sceneGate=1,1 overlayCounter=0`.
- corrected interpretation:
  - the full `27/12 + 27/11 + 27/4` trio is confirmed to repair the transient `sceneTickGate=0,1` branch, but it is not sufficient to satisfy the first scene-resource wait when `30/7` room roles is omitted.
  - the earlier `27/12 + 27/11` disappearance of WT49 must be treated as combo/timing evidence, not proof that the fb-target pair alone is the missing semantic.
- changed:
  - swapped the default 10-object experiment to keep `30/7` room roles and omit `30/3` room NPC.
  - new bundled order should be `5/10`, `10/26`, `30/1`, `30/7`, `12/1`, `7/42`, `6/1`, `27/12`, `27/11`, `27/4`.
- next verification:
  - rerun and check whether preserving `30/7` suppresses `WT len=49`.
  - if WT49 persists, the remaining likely blockers are still correctly encoded/timed `6/13`/`6/14` and/or safe `2/10 otherinfo`, not the fb-target trio by itself.

## 2026-06-08 rerun: 30/7 with full fb-target trio still does not suppress WT49

- verified from the newest manual run:
  - `mock_group_type1_response ... objects=10 ... sceneRoomNpc=0 sceneRoomRoles=1 ... len=528`.
  - dispatch order is `5/10`, `10/26`, `30/1`, `30/7`, `12/1`, `7/42`, `6/1`, `27/12`, `27/11`, `27/4`.
  - `30/7` reaches `sub_1039430()` through `trace_business_handler pc=01039430`.
  - `27/12` reaches `net_handle_fb_target_dispatch()` at `0x01010BC0`.
  - `case27_11` clears `R9+0x5C67` from `1 -> 0`; `case27_4` restores `R9+0x5C67/5C68` to `1,1`.
- negative result:
  - the client still sends `WT len=49` at tick 168 for `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10`, and `25/5`.
  - the standalone follow-up again hits `net_business_response_dispatch()` with `dispatchGate=0`.
  - `sub_1013C8A()` continues drawing the `loading.gif` widget with `sceneGate=1,1 overlayCounter=0`.
- static correction:
  - IDA shows `stream_reader_init_from_blob()` installs `stream_read_i16_be_tagged` at method slot `+0x24`, `stream_peek_i16_be` at `+0x2C`, and `stream_read_cstr_len16` at `+0x1C`.
  - case `6/13` at `0x01047896` reads each tasktype record as `tagged i16` followed by `len16 + NUL-terminated bytes`.
  - the previous `vm_net_mock_append_tasktypes_empty13_object()` used `vm_net_mock_put_object_blob()`, which prepended an extra data-length word before the raw stream. That made the case-13 cursor read from the wrong alignment.
- changed:
  - `tasktypes` now uses `vm_net_mock_put_object_raw()` so the field value begins directly with the six-record stream.
  - the next default first-window packet restores both `30/3` and `30/7` and bundles `12/1`, `7/42`, `6/1`, corrected `6/13`, and `6/14`.
  - expected order: `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `12/1`, `7/42`, `6/1`, `6/13`, `6/14`.
- next verification:
  - rerun and check whether corrected same-window `6/13` avoids the previous corrupted callback fault.
  - check whether `WT len=49` disappears; if it does, the remaining visible loading widget can be separated from the packet wait.

## 2026-06-08 rerun: corrected raw tasktypes still overruns inside case 13

- verified from the newest manual run:
  - the 10-object packet is accepted: `mock_group_type1_response ... objects=10 ... sceneRoomNpc=1 sceneRoomRoles=1 ... len=665`.
  - dispatch order reaches `6/13` at index 8 after `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `12/1`, `7/42`, and `6/1`.
  - `6/14` is not reached.
  - stdout now faults inside `mem_copy`: `pc=0104d404`, `lr=010478f3`, `r2=feffc07c`.
  - trace shows the same `mem_copy` path writing through the scene gate/watch region: `trace_scene_loading_owner_write ... pc=0104d410 lr=010478f3`.
- corrected interpretation:
  - changing `tasktypes` from `put_object_blob()` to `put_object_raw()` was not sufficient.
  - case `13` is still reading a bad name length by the time it enters the later record copies, so either the field wrapper semantics or the per-record string form remains mismatched.
- changed:
  - added read-only `trace_tasktypes_case13_stream` hooks at `0x0104789E`, `0x010478CE`, `0x010478E2`, and `0x010478EE`.
  - the next run will log the case-13 stream cursor, blob object, data pointer, blob length, next 16 bytes, loop index, destination pointer, and pending `memcpy` length before the copy that overruns.
- next verification:
  - rerun and inspect `trace_tasktypes_case13_stream` to determine whether the cursor starts at the wrong field location, whether the field descriptor's `+4/+8` values are unexpected, or whether the empty-string record encoding is wrong.

## 2026-06-08 rerun: tasktypes record scalar is tagged i8, not tagged i16

- verified from the newest manual run with `trace_tasktypes_case13_stream`:
  - after field lookup, `tasktypes` resolves to blob object `0500209c`, data pointer `0500218c`, length `42`.
  - after the first scalar read, cursor is `3`, not `4`: `trace_tasktypes_case13_stream label=before_peek_name_len ... r4=2 ... cursor=3`.
  - the next bytes at cursor 3 are `00 00 01 00 00 02 ...`; the leading extra `00` is the fourth byte from our old `tagged i16` encoding (`00 02 00 00`).
  - by the second loop, cursor is misaligned and `stream_peek_i16_be()` reads `02 00` as length `512`, leading to `mem_copy` length `0x200`; later it reads `0x9CE4` and faults.
- corrected static interpretation:
  - in case `6/13`, `R6` is already the stream method table base (`stream + 0x400`), so `[R6+0x28]` is method slot `+0x28`: `stream_read_i8_tagged()`.
  - the case-13 record grammar is therefore six records of `tagged i8 + len16 C string`, not `tagged i16 + len16 C string`.
- changed:
  - `vm_net_mock_append_tasktypes_empty13_object()` now emits each empty tasktype record as `vm_net_mock_seq_put_u8(0)` followed by `vm_net_mock_seq_put_string("")`.
- next verification:
  - rerun and confirm the case-13 cursor advances by 6 bytes per empty record and dispatch reaches `6/14`.
  - then check whether the client still sends `WT len=49`.

## 2026-06-08 rerun: tagged-i8 tasktypes is stable but not sufficient

- verified from the newest manual run:
  - the 10-object first-window packet is accepted: `mock_group_type1_response ... objects=10 ... len=659`.
  - dispatch reaches all intended objects: `5/10`, `10/26`, `30/1`, `30/3`, `30/7`, `12/1`, `7/42`, `6/1`, `6/13`, and `6/14`.
  - `trace_tasktypes_case13_stream` shows the corrected `6/13` stream ending at cursor `36` after six empty tagged-i8/name records; each empty-name copy length is `1`.
  - no stdout fault occurs.
- negative result:
  - the client still sends `WT len=49` at tick `147` for `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10`, and `25/5`.
  - the standalone follow-up is still blocked by `dispatchGate=0`.
  - `sub_1013C8A()` continues drawing the `loading.gif` widget with `sceneGate=1,1 overlayCounter=0`.
- changed:
  - downgraded `2/10 otherinfo` to a parser-safe empty response: `othernum=0`, empty `otherinfo`.
  - the next first-window experiment now defaults `30/3` room NPC off, keeps `30/7` room roles, and appends empty `2/10` after corrected `6/13` and `6/14`.
  - expected order: `5/10`, `10/26`, `30/1`, `30/7`, `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10(empty)`.
- validation:
  - `make` succeeds after the builder change.
- next verification:
  - rerun and check whether WT49 disappears or changes.
  - check whether the actor-adjacent protagonist-image fragments change.
  - check whether the top-center map name returns or remains missing.

## 2026-06-08 rerun: empty 2/10 is stable but WT49 still returns

- verified from the newest manual run:
  - the first-window packet is accepted: `mock_group_type1_response ... objects=10 ... sceneRoomNpc=0 sceneRoomRoles=1 ... len=536`.
  - dispatch order is `5/10`, `10/26`, `30/1`, `30/7`, `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10`.
  - corrected `6/13` still walks six empty records safely and reaches cursor `36`.
  - empty `2/10` reaches dispatch item index `9` and no stdout fault occurs.
  - the protagonist current node remains stable: `trace_scene_current_node_draw ... actorId=10001 ... grid=223,382 ... callback=01045579`.
- negative result:
  - the client still sends `WT len=49` at tick `304`.
  - the late `builtin-scene-resource-followup` response is still blocked by `dispatchGate=0`.
  - the steady visible strip/fragments are still the scene `loading.gif` widget path: `trace_loading_gif_widget_draw_entry ... caller=01013cba ... sceneGate=1,1 overlayCounter=0`, with paired `trace_progress_strip_wrapper_tick ... caller=010460ca ... dst=20,188`.
- map/status evidence:
  - the descriptor path still writes `statusText=蓬莱-铜雀台` at `0x0100F68A`.
  - `promptName_0x12_R9_5CD8` is empty, and the current first-window bundle omitted `30/3` room NPC.
  - hypothesis: the missing top-center map text may be tied either to the empty prompt-name branch or to omitting `30/3`; current evidence does not prove which.
- changed:
  - switched the next first-window experiment to answer the exact seven-object WT49 family in the open dispatch window.
  - defaults now omit both `30/3` and `30/7` room-list objects, then bundle `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, empty `2/10`, and `25/5`.
  - expected order: `5/10`, `10/26`, `30/1`, `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10(empty)`, `25/5`.
- validation:
  - `make` succeeds after the builder change.
- next verification:
  - rerun and check whether exact same-window WT49 coverage suppresses the later `WT len=49`.
  - if WT49 still appears, the missing readiness state is probably not just object presence; re-check room-list side effects and non-empty semantic content.

## 2026-06-08 rerun: exact WT49 family still leaves loading owner active

- verified from the newest manual run:
  - first-window response is accepted with `mock_group_type1_response ... objects=10 ... sceneRoomNpc=0 sceneRoomRoles=0 bundledSceneFollowup=1 ... len=424`.
  - dispatch order is exactly `5/10`, `10/26`, `30/1`, `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10`, `25/5`.
  - corrected `6/13` still reaches cursor `36`, empty `2/10` remains parser-safe, and `25/5` dispatches at index `9`.
  - the protagonist node is stable: `trace_scene_current_node_draw ... actorId=10001 ... grid=223,382 ... callback=01045579 drawAt=01045579`.
- negative result:
  - the client still sends the later `WT len=49` scene-resource request, and the standalone response is blocked by `dispatchGate=0`.
  - `sub_1013C8A()` still dispatches the scene `loading.gif` widget with `sceneGate=1,1 overlayCounter=0`; the visible progress strip is still the `caller=010460ca dst=20,188` loading-widget path.
  - top-center map text is still missing in the screenshot while trace shows `statusText=蓬莱-铜雀台` and empty `promptName_0x12_R9_5CD8`.
- corrected interpretation:
  - same-window object-header coverage for the exact WT49 family is not sufficient.
  - two standalone scene-bootstrap misc-player responses are still blocked after `30/1`: `1/7/7 type=2` maps to `kind=7 subtype=20` and reads `pcimg`; `type=3` maps to `kind=7 subtype=32` and reads `expcard` (IDA `net_handle_misc_player_fields` at `0x01011C88`).
- changed:
  - `vm_net_mock_build_group_type1_response()` now bundles `7/20 {result=1, pcimg=0}` and `7/32 {result=1, expcard=0}` in the still-open first dispatch window, before `30/1`.
  - the trace now prints `bundledType2Type3=1`; expected first-window object count is `12`.
- validation:
  - `make` succeeds after the builder change.
- next verification:
  - rerun and confirm dispatch includes `kind=7 subtype=20` and `kind=7 subtype=32` before `30/1`.
  - then check whether the loading/progress owner, actor-adjacent fragments, later `WT len=49`, and top-center map text change.

## 2026-06-08 rerun: 12-object first packet exceeds unpacker limit

- verified from the newest manual run:
  - the mock built `mock_group_type1_response ... objects=12 ... bundledType2Type3=1 ... bundledSceneFollowup=1 ... len=484`.
  - `net_business_response_dispatch()` did not accept that packet: `trace_business_dispatch_state label=unpack_error ... r0=00000003 ... objectCount=10`.
  - because the overfull packet never reached `30/1`, `sceneObj+0x164` stayed open (`dispatchGate=1`).
  - the later standalone `7/20.pcimg`, `7/32.expcard`, and the seven-object `WT49` scene-resource response were therefore consumed normally instead of taking `early_gate_off`.
- negative result:
  - even after the standalone `7/20`, `7/32`, and seven-object WT49 response were consumed, `sub_1013C8A()` still dispatched the scene `loading.gif` owner with `sceneGate=1,1 overlayCounter=0`.
  - the protagonist node stayed valid (`actorId=10001`, `callback=drawAt=0x01045579`), so the current issue is not a drawAt corruption recurrence.
  - top-center map text still correlates with empty `promptName_0x12_R9_5CD8`; `statusText` remains `蓬莱-铜雀台`.
- corrected interpretation:
  - the client unpacker has a practical 10-object ceiling for this response path; 12-object first-window bundling is invalid and should not be used as evidence for parser-side semantics.
  - consuming `7/20`, `7/32`, and the WT49 family is still not sufficient by itself when the initial group/type1 scene-enter fields were not consumed.
- changed:
  - `vm_net_mock_build_group_type1_response()` now stays under the limit: it returns only `5/10`, `10/26`, `7/20`, and `7/32` by default, leaving `sceneEnter=0` and `bundledSceneFollowup=0`.
  - `vm_net_mock_build_scene_resource_followup_response()` now appends trailing `30/1 scene+posinfo` after the requested seven-object WT49 family, so that the dispatch gate is closed only after those requested objects are consumed.
- validation:
  - `make` succeeds after the builder change.
- next verification:
  - rerun and confirm the first packet reaches `after_unpack_ok` with four objects.
  - confirm the later scene-resource response has eight objects ending in `kind=30 subtype=1`.
  - check whether loading/fragments and top-center map text change.

## 2026-06-08 rerun: valid 4-object + 8-object ordering still leaves local scene wait active

- verified from the newest manual run:
  - first response is parser-valid: `mock_group_type1_response ... objects=4 ... bundledType2Type3=1 ... len=189`.
  - `net_business_response_dispatch()` reaches `after_unpack_ok` with four objects and dispatches `5/10`, `10/26`, `7/20`, `7/32`.
  - the later `WT len=49` response is parser-valid with eight objects: `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, empty `2/10`, `25/5`, trailing `30/1`.
  - trailing `30/1` closes `sceneObj+0x164` only after the requested seven scene-resource objects dispatch: `trace_scene_dispatch_gate_write ... pc=01039766 ... old=1 new=0`.
  - no `unpack_error` or `early_gate_off` occurs in the relevant packets, and stdout has no fault.
  - the protagonist current node remains stable: `actorId=10001`, `grid=223,382`, `callback=drawAt=0x01045579`.
- confirmed negative result:
  - the visible loading/progress owner is still drawn by `sub_1013C8A()`/`loading.gif`: trace shows `caller=01013cba`, `sceneGate=1,1`, `gate5530=0`, `overlayCounter=0`.
  - there is no `trace_resource_request`/`sub_10365F0` or manager enqueue hit in this run, so `R9+0x9590` remains zero and satisfies the static loading condition at `0x01013C96..0x01013CB8`.
  - top-center map prompt remains empty in trace: `promptName_0x12_R9_5CD8=<empty>`, while `statusText_0x16_R9_5CDC=蓬莱-铜雀台`.
- corrected interpretation:
  - the previous packet-order/gate hypothesis is now mostly discharged for the current mock shape: the requested WT49 family is consumed before scene-enter gate close.
  - the remaining first-order suspect is the local scene descriptor path: `parse_actorinfo_response()` stores actorinfo's second tail string into `R9+0x5E46`, `scene_rebuild_runtime_nodes()` uses it as `currentMapIdText`, then hardcodes mode `10` with callback `sub_100F094`.
  - runtime evidence still shows `parse_actor_motion_descriptor()` receiving `name=c00蓬莱仙岛_01` and callback `0x0100F095`, then handing the `.sce` portal stream at cursor `135` to `sub_100F094`; `tools/inspect_sce.py` identifies that offset as the local portal to `00蓬莱仙岛_02.sce`.
- next target:
  - do not force scene globals or patch draw logic.
  - recover the expected first-enter `sceneKey/currentMapIdText + mode10 callback` policy: whether the server should send a different actorinfo tail string, whether the first mode-10 callback seed should be null until local prompt data exists, or whether a different local descriptor resource should feed `sub_100F094`.

## 2026-06-08 experiment: split actorinfo descriptor key away from local GBK `.sce`

- evidence driving the change:
  - `parse_actor_motion_descriptor()` only reaches its `sub_10365F0(6, ...)` queue writer on the `sub_100D534(name)==0` branch at `0x0100D7D2 -> 0x0100DB4A`.
  - when `sub_100D534(name)` succeeds, the parser consumes the returned stream and directly invokes callback `a8` at `0x0100DB32`.
  - the latest trace shows `scene_rebuild_runtime_nodes()` forwarding `stack0Name=c00蓬莱仙岛_01` into `parse_actor_motion_descriptor()`, and because local `JHOnlineData/c00蓬莱仙岛_01.sce` exists, that `.sce` portal stream reaches `sub_100F094` directly.
  - `30/1.scene` is still a separate packet field and remains on the concrete GBK scene resource path.
- changed:
  - `vm_net_mock_scene_key_name()` now defaults to ASCII `c00PenglaiXiandao_01` while still honoring `CBE_SCENE_KEY`.
  - `vm_net_mock_load_resource_update_payload()` now returns the existing minimal motion-descriptor wrapper when the client requests `c00PenglaiXiandao_01`.
  - this is a server/resource-response experiment, not a draw patch: no game globals are forced, `scene_draw_actor_pass` is untouched, and `30/1.scene` still uses the GBK `c00蓬莱仙岛_01`.
- hypothesis:
  - using a c-prefixed scene key with no colliding local `.sce` should prevent the portal grammar from being parsed as an actor/status stream, and may make the client exercise the resource queue/update path that was absent in the previous run.
- validation:
  - `make` succeeds after the change.
- next verification:
  - rerun manually and check whether `trace_scene_actorinfo_snapshot ... scene=c00PenglaiXiandao_01` appears.
  - check whether `trace_resource_request_enqueue`, `trace_update_request_prepare`, or `mock_motion_wrapper ... inner=...` appears for `c00PenglaiXiandao_01`.
  - check whether `trace_actor_motion_callback_handoff` still hands `.sce` portal bytes to `sub_100F094`.
  - check whether `trace_loading_gif_widget_draw_entry`, actor-adjacent fragments, and top-center map text improve or regress.

## 2026-06-08 rerun: ASCII descriptor key reaches update path, then waits on WT39

- confirmed from the newest manual run:
  - actorinfo now carries the non-colliding scene descriptor key: role-select response length changed to `466`, and later trace lines use `uiName=c00PenglaiXiandao_01`.
  - the client requests `18/7 type=6 name=c00PenglaiXiandao_01`; the mock serves the 44-byte minimal motion wrapper and the runtime writes it through `MMORPGTempbin` to `JHOnlineData/c00PenglaiXiandao_01`.
  - `parse_actor_motion_descriptor()` reopens that ASCII key and then `ui_h_war.actor` / `h_warriorwalk2.gif`; the callback handoff tail no longer matches the old `.sce` portal prefix.
  - `sub_100F094()` now produces `trace_actor_move_entry_table ... count=0 promptName=<null> statusText=<null>` from the minimal wrapper path.
  - immediately after that, the client sends a new unhandled request: `WT len=39 hdr=6/1 objs=1/6/1,1/6/13,1/6/14,1/2/10,1/25/5 count=5`, and the previous build asserts.
- corrected interpretation:
  - this run does not support the broad claim that the progress/loading state is only local residue. There is a confirmed server wait after the type=6 resource update.
  - the old portal-derived body/world entry evidence is superseded for this run: the bytes handed to `sub_100F094()` come from the downloaded ASCII descriptor wrapper, not from local `c00蓬莱仙岛_01.sce`.
  - the minimal wrapper is parser-safe enough to avoid the portal entry, but it is semantically incomplete for prompt/status text.
- changed:
  - `vm_net_mock_is_scene_resource_followup_request()` now accepts both the original full `WT len=49` family and the post-update `WT len=39` subset.
  - `vm_net_mock_build_scene_resource_followup_response()` now omits `12/1 + 7/42` when the request omits them, but still returns `6/1`, `6/13`, `6/14`, empty `2/10`, `25/5`, and trailing `30/1`.
- validation:
  - `make` succeeds after the builder change.
- next verification:
  - rerun manually and confirm `source=builtin-scene-resource-followup` handles the new `WT len=39` request.
  - then inspect whether the loading owner, actor-adjacent fragments, and top-center title move to a later or clearer failure point.

## 2026-06-08 rerun: cached ASCII descriptor removes sprites but also blanks the map

- confirmed from the current screenshot and logs:
  - the actor-adjacent sprite fragments are gone, but the map viewport is black.
  - this run did not send `18/7 type=6` or the new `WT len=39`, because `JHOnlineData/c00PenglaiXiandao_01` already existed from the previous run and was opened directly.
  - storage trace shows only the cached ASCII descriptor open/read for the mode-10 descriptor path: `JHOnlineData/c00PenglaiXiandao_01`, length `44`.
  - no `JHOnlineData/c00蓬莱仙岛_01.sce` open appears in this run, while the runtime still consumes the WT49 follow-up and trailing `30/1` successfully.
  - `trace_actor_move_entry_table ... count=0 promptName=<null> statusText=<null>` confirms the minimal descriptor avoids the old portal entry but also supplies no map title/status semantics.
- corrected interpretation:
  - the ASCII-key experiment is useful evidence but is not a valid default: it bypasses the local scene descriptor resource needed for the map/background path.
  - the original visible-fragment bug is still best treated as the portal record being published into the body/world move-entry table, not as proof that the scene key should avoid the local `.sce` entirely.
- changed:
  - `vm_net_mock_scene_key_name()` now defaults back to `vm_net_mock_default_scene_name()` so actorinfo's scene key matches `30/1.scene` and reopens the local GBK `.sce` path.
  - `CBE_SCENE_KEY` remains available to reproduce the ASCII descriptor/update experiment explicitly.
  - the WT49/WT39 scene-resource follow-up fixes remain in place.
- validation:
  - `make` succeeds after the default-key rollback.
- next verification:
  - rerun manually with default environment and confirm `trace_scene_actorinfo_snapshot ... scene=c00蓬莱仙岛_01`, local `.sce` open, and map background return.
  - check whether the actor-adjacent fragments stay gone via the current portal-entry append guard and whether the loading/title state changes.

## 2026-06-08 rerun: map/title restored, remaining fragments are the loading widget

- confirmed from the newest screenshot and logs:
  - the default GBK scene key restores the local map path: storage trace opens `JHOnlineData/c00蓬莱仙岛_01.sce`, and the screenshot shows the map background plus top-center title `蓬莱-铜雀台`.
  - the portal-derived body/world entry is not the current visible artifact: `trace_portal_move_entry_append_skipped ... actorId=1 grid=223,382 box=203,402,240,422 kind=2` fires, and `trace_actor_move_entry_table ... count=0`.
  - the protagonist current node remains stable: `trace_scene_current_node_draw ... actorId=10001 ... grid=223,382 ... screen=103,65 ... drawAt=01045579`.
  - the remaining actor-adjacent "fragments" are the scene loading/progress widget, not body/world actor draw: `trace_loading_gif_widget_draw_entry ... caller=01013cba ... sceneGate=1,1 overlayCounter=0 ... args=20,188,200`, with the tiled `trace_progress_strip_wrapper` region overlapping the screenshot artifact.
- changed:
  - the full WT49 `builtin-scene-resource-followup` response now appends a hypothesis-only fb-target seed pair, `27/12` followed by empty `27/11`, before the trailing `30/1`.
  - the response stays at the practical 10-object limit: `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, empty `2/10`, `25/5`, `27/12`, `27/11`, trailing `30/1`.
  - the smaller WT39 subset path does not append the fb-target seed yet, because this experiment is tied to the full fresh-enter WT49 family.
- hypothesis:
  - after the now-correct resource/task/other family is consumed, the remaining `sub_1013C8A()` owner may still need the fb-target `27/12 + 27/11` state transition, but not `27/4`; previous evidence shows `27/4` restores the owner gate to `1,1` and keeps the loading widget visible.
- validation:
  - `make` succeeds after the builder change.
- next verification:
  - rerun manually and check for `mock_scene_resource_followup_response ... objects=10 ... fbSeed=1`.
  - confirm whether `trace_followup_case label=case27_11` appears after the WT49 response.
  - compare `trace_loading_gif_widget_draw_entry` and the visible fragments after that point; if the widget stops but the scene enters a `sceneGate=0,1` branch, revisit the missing finalize/room-list semantics rather than patching drawing.

## 2026-06-08 rerun: proactive 27/11 stops loading.gif but enters gate 0,1

- confirmed from the latest manual run:
  - the full WT49 response with fb seed pair is parser-valid: `mock_scene_resource_followup_response objects=10 ... fbSeed=1 ... len=373`, followed by `after_unpack_ok ... objectCount=10`.
  - follow-up scanning reaches `case27_11`: `trace_followup_case label=case27_11 pc=010106d8 ... tick=280`.
  - the old `loading.gif` owner no longer persists after that transition, but the scene immediately moves into the other loading branch: repeated `trace_loading_overlay_call label=sub_1003568/sub_100337C ... sceneTickGate=0,1`.
  - this matches IDA for `net_handle_business_followup_events()` at `0x010106F4`, where subtype `27/11` writes `R9+0x5C67 = (sceneObj+976 != 1)`.
- corrected interpretation:
  - proactively appending empty `27/11` in the WT49 response is too early/incomplete. It clears gate A before the matching finalize path runs and leaves `scene_runtime_tick()` in its `sub_1003568()` early-loading branch.
  - older evidence showed `27/12` alone can unlock a real client follow-up request carrying `27/11` and `27/4(type=1)`, so the better next server-shape experiment is to seed only `27/12` and let the client request the finalize chain.
- changed:
  - the WT49 scene-resource-followup experiment now appends only `27/12(result=1, fb=1, name=sceneKey, posinfo=spawn)` before trailing `30/1`.
  - response order is now `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, empty `2/10`, `25/5`, `27/12`, `30/1`.
  - trace marker changed to `fbSeed12=1`.
- validation:
  - `make` succeeds after the builder change.
- next verification:
  - rerun manually and check for `mock_scene_resource_followup_response ... objects=9 ... fbSeed12=1`.
  - check whether the client emits the expected later `WT len=34`-class request and whether `mock_type27_followup_combo_response` handles it.
  - then compare whether `trace_loading_gif_widget_draw_entry` or `trace_loading_overlay_call sceneTickGate=0,1` remains in the tail.

## 2026-06-08 rerun: WT49 27/12-only accepted but no client finalize request

- verified from the newest manual run:
  - the full WT49 response is parser-valid with 9 objects: `mock_scene_resource_followup_response objects=9 ... fbSeed12=1 ... len=367`.
  - `net_business_response_dispatch()` reaches `after_unpack_ok ... objectCount=9` and dispatches the seed object: `trace_business_handler pc=01010bc0 packet=05001668 kind=27 subtype=12`.
  - trailing `30/1` still dispatches afterward and closes the business dispatch gate normally.
- confirmed negative:
  - no later `WT len=34`-class request appears in `net_packets.log` / `net_trace.log`.
  - no `mock_type27_followup_combo_response` appears.
  - the scene remains on the original `sub_1013C8A()` loading-widget path: repeated `trace_loading_gif_widget_draw_entry ... caller=01013cba ... sceneGate=1,1 overlayCounter=0`.
- changed:
  - full WT49 scene-resource-followup responses now append `27/12` followed by `27/4(result=1,type=1,fb=1,info=<scene title>)`, then trailing `30/1`.
  - this still avoids `27/11`, because the previous accepted run proved `27/11` clears `R9+0x5C67` and enters `sceneTickGate=0,1`.
  - `vm_net_mock_append_fb_target_result4_object()` now accepts an explicit `info` string. Existing type27 combo responses keep `info=""`; only the WT49 experiment uses the default GBK `蓬莱-铜雀台` title, overridable with `CBE_FB_TARGET_INFO`.
- next verification:
  - rerun manually and look for `mock_scene_resource_followup_response ... objects=10 ... fbSeed12Ready4Info=1`.
  - expected parser trace is `case27_4` without `case27_11`.
  - then compare whether the loading-widget condition (`sceneGate=1,1 overlayCounter=0`) changes or whether a new packet request appears.

## 2026-06-08 rerun: non-empty 27/4 shows map prompt but loading widget remains

- verified from the newest manual run:
  - the full WT49 response with `27/12 + 27/4(info)` is parser-valid: `mock_scene_resource_followup_response objects=10 ... fbSeed12Ready4Info=1 ... len=432`.
  - primary dispatch reaches both fb-target objects:
    - `trace_business_handler pc=01010bc0 ... kind=27 subtype=12`
    - `trace_business_handler pc=01010bc0 ... kind=27 subtype=4`
  - follow-up scanning reaches `case27_4` without `case27_11`.
  - the non-empty `info` branch has a visible effect: the user observed the map-name prompt after entering the map.
- confirmed negative:
  - after `case27_4`, the scene loading owner condition is unchanged: repeated `trace_loading_gif_widget_draw_entry ... caller=01013cba ... sceneGate=1,1 overlayCounter=0`.
  - the actor/body move-entry table remains empty (`trace_actor_move_entry_table ... count=0`), so the visible tiles beside the protagonist still belong to the loading/progress skin path, not body/world actor drawing.
- sequencing evidence:
  - `27/12` already clears `sceneObj+0x164` at `0x0100EA6A`; the trailing `30/1` in this run only writes the same gate to `0` again at `0x01039766`.
  - this makes trailing `30/1` a candidate slot to spend on `27/11` while preserving the 10-object parser limit.
- changed:
  - full WT49 scene-resource-followup responses now replace trailing `30/1` with `27/11`, producing tail order `27/12`, `27/11`, `27/4(info)`.
  - WT39 subset responses still append trailing `30/1`.
- validation:
  - `make` succeeded after this builder change.
- next verification:
  - rerun manually and look for `mock_scene_resource_followup_response ... objects=10 ... fbFull12_11_4Info=1 trailingSceneEnter=0`.
  - expected trace: `case27_11` followed by `case27_4`, with final writes returning `R9+0x5C67/0x5C68` to `1,1`.
  - then check whether `sub_1013C8A()` still draws with `overlayCounter=0`.

## 2026-06-08 rerun: full 27 trio still leaves loading widget; movement upload appears

- verified from the newest manual run:
  - the full WT49 response shape under test appeared as `mock_scene_resource_followup_response objects=10 ... fbFull12_11_4Info=1 trailingSceneEnter=0 len=390`.
  - dispatch reached `27/12`, `27/11`, and `27/4`; follow-up scanning reached `case27_11` and then `case27_4`.
  - gate writes matched static expectations: `case27_11` cleared `R9+0x5C67`, then `case27_4` restored `R9+0x5C67/0x5C68` to `1,1`.
- confirmed negative:
  - the visible strip/fragments remain the scene loading widget path: repeated `trace_loading_gif_widget_draw_entry ... caller=01013cba ... sceneGate=1,1 overlayCounter=0`.
  - this downgrades the "complete `27/12 -> 27/11 -> 27/4(info)` trio clears loading" idea to confirmed negative for this WT49 slot.
- new blocker:
  - after manual click/move, the client sent `WT len=32 hdr=2/1 objs=1/2/1 count=1` containing `moveinfo`, and the previous mock asserted on `unhandled_packet`.
  - IDA evidence from `net_handle_actor_move_info()` (`0x01012ADC`): subtype `1` falls through the default return because the switch subtracts `2`; subtype `2` is the first branch that reads `moveinfo` at `0x01012B2E`.
- changed:
  - added `builtin-actor-moveinfo-ack`, a minimal empty `1/2/1` response for one-object `moveinfo` uploads.
  - this is a parser-safe ack only; the actual movement-upload semantics remain unresolved.
- validation:
  - `make` succeeds after the ack builder change.
- next verification:
  - rerun manually and look for `mock_default source=builtin-actor-moveinfo-ack`.
  - check whether the client proceeds to another packet or whether `trace_loading_gif_widget_draw_entry ... overlayCounter=0` continues unchanged after movement.

## 2026-06-08 screenshot/log check: restored map/title, stable loading widget, no moveinfo upload

- verified from the newest screenshot and logs:
  - the top-center title and map background are visible again, and the protagonist draw path is stable.
  - `net_packets.log` ends at the accepted WT49 scene-resource follow-up; this run did not emit `2/1 moveinfo`, so the new `builtin-actor-moveinfo-ack` was not exercised.
  - the remaining artifact still matches the loading/progress owner: `trace_loading_gif_widget_draw_entry ... caller=01013cba ... args=20,188,200`, with `sceneGate=1,1` and `overlayCounter=0`.
  - `trace_actor_move_entry_table ... count=0` remains true, so the old body/world entry root cause is not back.
- static evidence:
  - `sub_1013C8A()` at `0x01013C8A` calls the widget draw only when `s16[R9+0x9590] <= 0` and both scene loading owner bytes at `R9+0x5C67/+0x5C68` are nonzero.
- changed:
  - the full WT49 response now restores trailing `30/1 scene+posinfo` after the `27/12`, `27/11`, `27/4(info)` trio.
  - to keep the accepted 10-object limit, the full WT49 shape temporarily omits `25/5`; the WT39 subset still includes `25/5` plus trailing `30/1`.
  - expected full WT49 order: `12/1`, `7/42`, `6/1`, `6/13`, `6/14`, empty `2/10`, `27/12`, `27/11`, `27/4(info)`, `30/1`.
- validation:
  - `make` succeeds after the restored `30/1` builder change.
- next verification:
  - rerun manually and look for `mock_scene_resource_followup_response ... result25_5=0 fbFull12_11_4Info=1 trailingSceneEnter=1`.
  - check whether `post_data_event ... sceneState` returns to the earlier `7` and whether `trace_loading_gif_widget_draw_entry ... overlayCounter=0` stops or changes.

## 2026-06-08 rerun: restored 30/1 and moveinfo ack both negative for loading widget

- verified from the newest manual run:
  - the restored full-WT49 shape is active and parser-valid: `mock_scene_resource_followup_response objects=10 ... result25_5=0 fbFull12_11_4Info=1 trailingSceneEnter=1 len=420`.
  - follow-up dispatch reaches `case27_11` and then `case27_4`; `case27_11` clears `R9+0x5C67`, and `case27_4` restores `R9+0x5C67/0x5C68` to `1,1`.
  - trailing `30/1` restores the earlier scene-enter side effect: `runtime_state label=post_data_event ... sceneGate=0 sceneState=7 loadFlags=0,0,0`.
  - the client later emits `WT len=32 hdr=2/1 objs=1/2/1` with `moveinfo`, and the new `builtin-actor-moveinfo-ack` responds with an empty `1/2/1` object.
- confirmed negative:
  - restoring `30/1` after the fb-target trio does not stop the visible loading/progress widget.
  - the `2/1 moveinfo` ack is runtime-exercised and does not expose a new packet wait; because `30/1` has already closed the business dispatch gate, the ack response takes `early_gate_off`, which is acceptable for subtype `1` on current static evidence.
  - the steady tail remains `trace_loading_gif_widget_draw_entry ... caller=01013cba ... sceneGate=1,1 overlayCounter=0`, matching `sub_1013C8A()`'s static branch inputs at `0x01013C96..0x01013CB8`.
  - no `trace_resource_request_enqueue`, `trace_resource_request_mark`, or `netManagerCount_R9_9590` write appears in the run.
- current conclusion:
  - the WT49 tail-order/fb-target/scene-enter packet-shape hypotheses are now mostly discharged for the current mock.
  - the remaining first-order problem is the local scene descriptor/manager contract: `parse_actor_motion_descriptor()` still opens `c00蓬莱仙岛_01`, hands the local `.sce` portal tail to `sub_100F094`, the narrow portal candidate is skipped, and no valid manager/replay entry is queued to make `R9+0x9590` nonzero or otherwise stop `sub_1013C8A()`.
- next:
  - continue static/runtime recovery of the `R9+0x9588` manager/replay producer path and the correct fresh-enter `{sceneKey/currentMapIdText, mode=10, callback=sub_100F094}` policy before changing any more server response fields.

## 2026-06-08 instrumentation: split local descriptor parse vs manager replay

- latest log check before adding new probes:
  - the stable artifact still comes from `trace_loading_gif_widget_draw_entry pc=010461A8 lr=01013CBB caller=01013CBA ... args=20,188,200 sceneGate=1,1 overlayCounter=0`.
  - the accepted WT49 response is still `mock_scene_resource_followup_response objects=10 ... result25_5=0 fbFull12_11_4Info=1 trailingSceneEnter=1 len=420`.
  - manual movement is handled by `builtin-actor-moveinfo-ack`; the delivered ack takes `early_gate_off`, consistent with the current static subtype-1 default-return evidence.
- changed:
  - added read-only trace `trace_actor_motion_open_result` at `0x0100D702`, after `parse_actor_motion_descriptor()` copies the `sub_100D534(name)` return into `R4`, to classify the next run as local-open-success/direct-parse vs local-open-fail/enqueue.
  - added read-only trace `trace_actor_motion_enqueue_fallback` at `0x0100DB4A` and `0x0100DB74` to catch the fallback path and final `sub_10365F0` enqueue call arguments if the local descriptor open fails.
  - added read-only trace `trace_manager_replay_entry` at `0x01036768` to dump the active `R9+0x9588` manager record before `scene_hud_main_panel_sync_message()` consumes it.
- validation:
  - `make` succeeds after these trace-only changes.
- next verification:
  - rerun manually and compare whether the default GBK scene key logs `local_open_success_direct_parse` for `c00蓬莱仙岛_01`.
  - if the fallback/enqueue trace is absent and manager replay count stays zero, the loading widget is more likely a local descriptor/callback semantic issue than a still-pending server packet.

## 2026-06-08 rerun: GBK `.sce` direct-parse confirmed; no manager replay

- verified from the newest manual run and screenshot:
  - the map-title prompt is visible with `蓬莱-铜雀台`, confirming the non-empty `27/4.info` side effect is live.
  - `parse_actor_motion_descriptor()` opens the default GBK scene key locally: `trace_actor_motion_open_result ... stream=0501F640 name=c00蓬莱仙岛_01 mode=10 callback=0100F095 branch=local_open_success_direct_parse`.
  - no `trace_actor_motion_enqueue_fallback` and no `trace_manager_replay_entry` appears, so this run does not enter the `get_net_manager_object()->sub_10365F0(type=6, ...)` fallback path.
  - `sub_100F094` still consumes the `.sce` tail at cursor `135`; the first candidate is the known portal-shaped record and is skipped by the narrow guard: `trace_portal_move_entry_append_skipped ... actorId=1 grid=223,382 box=203,402,240,422 kind=2`.
  - after the skip, `trace_actor_move_entry_table ... count=0 promptName=<empty> statusText=蓬莱-铜雀台`.
- loading-owner evidence:
  - fresh scene init sets both owner bytes: `trace_scene_loading_owner_write ... pc=010132C8/010132CA ... old=0 new=1`.
  - `27/11` clears `R9+0x5C67`, then `27/4` restores `R9+0x5C67/+0x5C68` to `1,1`.
  - no `netManagerCount_R9_9590` write appears, so `sub_1013C8A()` continues to satisfy its draw condition (`R9+0x9590 <= 0` and both owner bytes nonzero).
- current conclusion:
  - this is now confirmed not to be an outstanding server download wait on the default path. The default path is local `.sce` direct-parse plus a hardcoded `sub_100F094` tail callback.
  - the remaining issue is the local scene-resource/overlay contract: either a real response family should create a `R9+0x9588` record later, or the current `.sce` tail callback/portal handling is still missing a side effect beyond the skipped portal entry.

## 2026-06-08 instrumentation: split loading-widget entry from message-box path

- corrected interpretation from the latest log/screenshot pair:
  - `loading_gif_widget_draw_frame()` only blits the center strip while widget byte `+0x10` is nonzero. The newest run shows the real `caller=0x010460CA dst=20,188` blit around ticks `1129..1130`, but later repeated `trace_loading_gif_widget_draw_entry` samples have `flag10=0`.
  - therefore the large green centered panel with `蓬莱-铜雀台` should no longer be treated as confirmed continuing progress-strip drawing. It is now a separate `sub_1013BDC()` / `sub_10110E6()` message-box hypothesis until traced directly.
- static evidence:
  - IDA `sub_1013BDC()` (`0x01013BDC`) calls `sub_10110E6(&loc_1013DF8, sub_101140C, sub_10108F4, 0,0,0,0)` at `0x01013C28` for the scene wait-callback branch, reuses the same call target from the state-3 branch via `0x01013C52`, and calls `sub_10110E6(&loc_1013E1C, ...)` at `0x01013C84` for the widget-frame timeout branch.
  - IDA `sub_10110E6()` (`0x010110E6`) either queues a message via `sub_101037E()` when `R9+0x5D44 == 1`, or calls `ui_show_message_box(a1,a2,a3,a4)` at `0x01011120` and sets message-active state.
- changed:
  - added read-only `trace_scene_message_request` at `0x01013C28`, `0x01013C52`, `0x01013C84`, `0x010110E6`, `0x01011112`, `0x01011120`, and `0x0101037E`.
  - the trace dumps the message text pointer/bytes, callbacks, scene gates, `R9+0x5530`, widget `+0x10/+0x0A`, `R9+0x9590`, and message flags around `R9+0x5D44/+0x5D80/+0x5D84`.
- validation:
  - `make` succeeds after the trace-only change.
- next verification:
  - rerun manually and compare `trace_scene_message_request` labels against the visible green map-name panel.
  - if only `message_box_branch` fires with the map-title text and widget `flag10=0`, then the remaining visible "fragments" in that screenshot are probably protagonist pixels behind the modal rather than a continuing loading strip.
  - if `scene_widget_timeout_message` fires first with widget frame `>300`, then the timeout branch is still live and the missing completion signal remains local/runtime rather than server packet ordering.

## 2026-06-08 rerun: actor+0x10A is overhead badge, not body direct image

- verified from the newest manual run:
  - no `trace_scene_message_request` line appears, so this run did not hit the traced `sub_1013BDC -> sub_10110E6` message-box path.
  - the actual loading GIF blit remains short-lived: `trace_progress_strip_wrapper_tick ... caller=010460CA dst=20,188` appears only around `tick=684`, and the long tail of `trace_loading_gif_widget_draw_entry` has `flag10=0`.
  - body/world move-entry draw is still absent (`trace_scene_body_draw_dispatch` count is zero), while the protagonist current-node path is stable: `actorId=10001`, `visual=1,0`, `visualRes=054045A8`, `grid=223,382`.
  - `trace_scene_actorinfo_snapshot` still shows the suspect field: `preview=h_warriorwalk1.gif`, `actor=h_warrior.actor`, `scene=c00蓬莱仙岛_01`.
- static evidence:
  - IDA `scene_draw_node_overhead_overlay()` (`0x01045578`) consumes the current node:
    - `a1+68` as overhead text
    - `a1+266` (`actor+0x10A`) as an optional named HUD/actor asset badge, guarded by `sub_1000342(a1+266) > 0` before resolving/drawing it at `0x010456AC..0x01045834`.
  - this final consumer contradicts the older hypothesis that `actor+0x10A` must be a body direct-image field. Filling it with `h_warriorwalk1.gif` plausibly explains the visible actor-art fragments beside/above the protagonist.
- changed:
  - `vm_net_mock_actor_preview_image_name()` now defaults to an empty string, while still honoring `CBE_ACTOR_PREVIEW_IMAGE` for experiments.
  - the `.actor` resource field (`actor+0xD8`), visual selector, scene key, grid, and target fields are unchanged.
- validation:
  - `make` succeeds after the builder-only change.
- next verification:
  - rerun manually and check that `trace_scene_actorinfo_snapshot ... preview=<empty>` (or blank) appears.
  - if the actor-adjacent fragments disappear while the protagonist remains visible, mark the root cause as confirmed mismatch: server mock wrote a body GIF into the optional overhead badge field.

## 2026-06-08 rerun: post-scene-change WT44 task subset handled

- verified from the newest manual run:
  - the `previewImage(+0x10A)` default-empty change fixes the actor-fragment symptom visually: protagonist remains visible and the stray walk-GIF fragments are gone.
  - after manual movement/transition, the client emits a new unhandled packet:
    - `WT len=44 hdr=25/5 objs=1/25/5,1/6/1,1/6/13,1/6/14,1/2/10,1/25/5 count=6`
    - payload includes final `Type=101`, matching the already known task/other/banner follow-up family from the earlier WT49 request.
- interpretation:
  - this is the WT49 scene-resource follow-up subset after a scene-change path, without `12/1` and `7/42`.
  - because the request no longer carries a scene key, appending the old default trailing `30/1` would risk re-entering the previous default map. Treat that trailing scene-enter side effect as unconfirmed for this WT44 shape.
- changed:
  - added `builtin-scene-task-subset-followup`, which recognizes the WT44 subset and replies only with already parser-safe objects:
    - `6/1 taskinfo=<empty>`
    - `6/13 tasktypes=<six empty tagged-i8 records>`
    - `6/14 action=0, tasknum=0, taskinfo=<empty>`
    - `2/10 othernum=0, otherinfo=<empty>`
    - `25/5 result=4`
  - no `30/1` is appended for this subset.
- validation:
  - `make` succeeds after the builder change.
- next verification:
  - rerun manually and look for `mock_default source=builtin-scene-task-subset-followup`.
  - then check whether the client proceeds to another packet family or whether the scene-change loading gates remain at `0,0`/`1,1`.

## 2026-06-08 side fix: restore portal move-entry after preview fix

- user screenshot after the `previewImage(+0x10A)` empty-default fix confirms the actor-art fragments are gone and the protagonist is visible, but the bottom-center transfer marker is missing.
- corrected implication:
  - the older `0x0100F468` portal append guard is now too broad. It was added while the fragment source was still ambiguous, but the newer final-consumer evidence points the fragments at `actor+0x10A` / overhead badge misuse instead.
  - the skipped record (`actorId=1`, `grid=223,382`, trigger box `203,402,240,422`, `kind=2`) matches the local `.sce` portal/transfer point and should be allowed into the body/world move-entry table.
- changed:
  - removed the active `vm_should_skip_portal_move_entry_append()` bypass from the `0x0100F468` hook path.
  - kept normal `trace_actor_move_entry_append` logging so the next rerun can show whether the portal entry is appended and drawn without reintroducing actor-art fragments.
- validation:
  - `make` compiled `src/main.c` but failed at link because `bin/main.exe` was locked by a running emulator process (`cannot open output file bin/main.exe: Permission denied`).
- next verification:
  - close the running emulator, rebuild, then rerun manually.
  - expected result: `trace_actor_move_entry_append ... actorId=1 grid=223,382 ... kind=2` returns, bottom-center transfer marker is visible again, and `trace_scene_actorinfo_snapshot ... preview=` remains empty so the old fragments do not return.

## 2026-06-08 side fix: transfer-load `+num.gif` path decoding

- confirmed from the latest transfer crash/run:
  - after stepping onto the restored portal, the client does not emit a new unhandled network request before the crash; `net_packets.log` ends after normal `2/1 moveinfo` acks and a safe `2/10` otherinfo response.
  - the new-map local load reaches `trace_actor_motion_descriptor_context ... name=00蓬莱仙岛_02.sce`, appends two portal/body move entries from that `.sce`, sends `scene_send_map_enter_request`, and then enters local piclib/resource preload.
  - the last storage trace is the piclib request for `+num.gif`: raw memory contains the full UCS-2 path `./\JHOnlineData\+num.gif`, but `vm_read_path_string()` decoded it as only `.` and `vm_get_file_handle()` returned handle `4` through the pseudo-dir path instead of opening the real file.
- root cause:
  - platform path detector `vm_path_looks_like_ucs2_le()` allowed ASCII path characters such as `/`, `\`, `.`, `_`, and `-`, but not `+`.
  - `+num.gif` was therefore misclassified as a narrow C string and truncated at the first UCS-2 NUL after `.`, making the later resource reader consume a pseudo-directory handle.
- changed:
  - `src/vmFunc.c::vm_path_looks_like_ucs2_le()` now accepts `+` as a valid ASCII path character in UCS-2 paths.
- evidence:
  - runtime trace: `trace_piclib_resource_dispatch ... name=+num.gif` followed by storage `file_open_request ... name=.` and `file_open_raw_name ... hex=2e002f005c004a...5c002b006e0075006d002e00670069006600`.
  - local asset check: `bin/JHOnlineData/+num.gif` exists and has a plausible wrapped length header (`0x00000271`, file size `629`).
- next verification:
  - rebuild, rerun manually, and confirm storage now logs `file_open handle=... path=JHOnlineData/+num.gif` plus the expected `file_read` lines before any later transfer-load fault.

## 2026-06-08 side fix: scene-change response follows requested mapID

- verified from the next manual run:
  - the previous `+num.gif` bug is fixed: `storage_trace.log` now records `file_open handle=4 path=JHOnlineData/+num.gif`, reads length `0x271`, reads the 625-byte body, and closes the file.
  - the client then advances to a new blocker: full-screen loading with repeated `trace_loading_overlay_call ... overlayState=3`.
- confirmed mismatch:
  - the live transfer request is `WT len=89` with `1/2/3 mapID=00蓬莱仙岛_02.sce exitID=1`, plus `1/27/11`, `1/27/4(type=1)`, and `1/7/42`.
  - the old `mock_scene_change_combo_response` replied with fixed `30/2.scene=c00蓬莱仙岛_01` and default spawn `(223,382)`, then sent `27/11` before `27/12` and omitted requested `27/4` / `7/42`.
  - runtime evidence after that reply was `sceneState=1`, `loadFlags=1,0,0`, dispatch gate closed, and the loading screen resumed.
- local scene evidence:
  - `tools/inspect_sce.py bin/JHOnlineData/00蓬莱仙岛_02.sce` shows `entry_id=1` spawn point `(128,45)`.
  - `net_trace.log` already showed the local descriptor path opening `00蓬莱仙岛_02.sce` before the server response, so the server should not force the scene name back to `_01`.
- changed:
  - `src/main.c::mock_scene_change_combo_response` now derives scene and `posinfo` from the requested `mapID/exitID`.
  - for `00蓬莱仙岛_02.sce + exitID=1`, it sends scene `00蓬莱仙岛_02.sce` and `posinfo=(128,45)`.
  - the response now includes `27/12` before `27/11`, then requested `27/4`, and `7/42` when requested.
- validation:
  - `make` succeeds after the scene-change builder change.
- next verification:
  - rerun manually and confirm `mock_scene_change_combo_response ... scene=00蓬莱仙岛_02.sce exitId=1 pos=128,45` appears.
  - if loading still persists, compare whether `sceneState` reaches `7` or remains `1`, and whether a later `25/5` or `27/*` packet is still being queued after the dispatch gate closes.

## 2026-06-08 follow-up: scene-change combo needs scene-aware trailing `30/1`

- verified from the latest manual run:
  - the second-map transfer response is now scene-correct: `mock_scene_change_combo_response ... scene=00蓬莱仙岛_02.sce exitId=1 pos=128,45`.
  - the follow-up `WT len=44 hdr=25/5 objs=1/25/5,1/6/1,1/6/13,1/6/14,1/2/10,1/25/5 count=6` is handled by `builtin-scene-task-subset-followup`, so this is no longer an unhandled-packet wait.
  - adding scene-aware `30/1` to the WT44 response is too late: the latest run shows `mock_scene_task_subset_followup_response ... trailingSceneEnter=1 ... len=228`, but dispatch immediately exits through `trace_business_dispatch_state label=early_gate_off ... dispatchGate=0 objectCount=6`.
  - because the late `30/1` is never consumed, runtime keeps the real loading widget active: `trace_loading_gif_widget_draw_entry ... flag10=1 gate5530=1` and paired `trace_progress_strip_wrapper_tick ... caller=010460ca dst=20,188`.
- static evidence:
  - IDA `net_handle_scene_channel_dispatch()` (`0x01039B8A`) sends subtype `1` to `parse_scene_response_entry()` and subtype `2` to `parse_scene_posinfo_field()`.
  - runtime from the first scene-resource follow-up shows the same contract: after dispatching trailing `kind=30 subtype=1`, `runtime_state ... sceneState=7 loadFlags=0,0,0` and subsequent loading-widget entries have `gate5530=0`.
- changed:
  - `mock_scene_change_combo_response()` now appends trailing `30/1 scene+posinfo` in the same dispatch window as `30/2`, using the requested scene-change target instead of the default scene key.
  - `vm_net_mock_build_scene_task_subset_followup_response()` is back to parser-safe task/other/banner objects only; its event is expected to arrive after the dispatch gate has already closed.
- validation:
  - `make` succeeds after the builder-only change.
- next verification:
  - rerun manually and check for `mock_scene_change_combo_response ... objects=6 ... trailingSceneEnter=1 scene=00蓬莱仙岛_02.sce pos=128,45`.
  - expected runtime evidence is `trace_business_handler ... kind=30 subtype=1` within the scene-change combo dispatch, followed by `runtime_state ... sceneState=7 loadFlags=0,0,0`, no `scene_widget_timeout_message`, and no moving center strip after the map-title prompt.

## 2026-06-09 rerun: combo `30/1` consumed but loading gate remains

- verified from the newest available log set:
  - the scene-change combo now has the intended shape: `mock_scene_change_combo_response objects=6 ... trailingSceneEnter=1 scene=00蓬莱仙岛_02.sce exitId=1 pos=128,45 len=303`.
  - the trailing scene-enter object is consumed before the dispatch gate closes: `trace_business_dispatch_item ... index=5 ... kind=30 subtype=1` and `trace_business_handler ... kind=30 subtype=1`.
  - after that dispatch, the client is back on the expected scene state: `runtime_state ... sceneGate=0 sceneState=7`, but the first snapshot still has `loadFlags=1,0,0`.
  - the WT44 task subset response is handled and arrives after the scene dispatch gate is closed: `trace_business_dispatch_state label=early_gate_off ... objectCount=5`, then `runtime_state ... loadFlags=0,0,0`.
  - the visible moving strip still persists after the handled packets: `trace_loading_gif_widget_draw_entry ... flag10=1 gate5530=1 ... activeScreen=01053f78`, paired with `trace_progress_strip_wrapper_tick ... caller=010460ca dst=20,188`.
- corrected interpretation:
  - scene-aware `30/1` in the combo is now confirmed partial, not sufficient. It restores the `sceneState=7` side effect but does not clear the second-map loading owner.
  - there is no new unhandled WT44 packet in this log. The next boundary is the local owner gate read as `R9+0x5530` by `loading_gif_widget_draw()` / `sub_1013BDC()`, not another blind reshuffle of WT44 fields.
- instrumentation status:
  - `src/main.c` now includes read-only write tracing for `R9+0x552C`, `R9+0x5530`, and `R9+0x5531`.
  - the inspected logs are older than the rebuilt `bin/main.exe`, so the lack of `loadingGate_R9_5530` write lines in this log is not evidence that the field is never written.
- validation:
  - `make` succeeds with the added loading-owner write trace.
- next verification:
  - rerun manually with the current `bin/main.exe` and inspect for `trace_scene_loading_owner_write field=loadingGate_R9_5530`.
  - if the only clear is still the `scene_widget_timeout_message` branch, analyze the `sub_1013BDC()` callback/`R9+0x552C` completion condition before changing more packet builders.

## 2026-06-09 rerun: WT44 late response leaves `R9+0x5530` stuck

- verified from the new write-trace run:
  - this run used the rebuilt binary: `net_trace.log` is newer than `bin/main.exe` and contains `loadingGate_R9_5530` trace lines.
  - the first scene flow still demonstrates the normal clear path: when WT49 dispatches while the scene business gate is open, handlers such as `7/42` and `27/4` clear `R9+0x5530`, and later widget entries have `gate5530=0`.
  - on the second-map scene-change path, the combo response is consumed correctly:
    - `mock_scene_change_combo_response objects=6 ... trailingSceneEnter=1 scene=00蓬莱仙岛_02.sce exitId=1 pos=128,45`
    - `trace_business_handler ... kind=30 subtype=1`
    - `runtime_state ... sceneGate=0 sceneState=7`
  - immediately after that, the client sends the WT44 task subset. The send wrapper `sub_103478E()` copies the outgoing request object's byte `+5` into `R9+0x5530` at `0x01034796`, setting it to `1`.
  - the mock WT44 response arrives after `30/1` has closed `sceneObj+0x164`, so `net_business_response_dispatch()` exits through `early_gate_off` at `0x01012F42`.
  - `net_wrapper_event_dispatch()` clears only `R9+0x5531` at `0x010348F6`; no business parser runs to clear `R9+0x5530`.
- conclusion:
  - the remaining moving strip is no longer an unknown local residue and no longer an unhandled-packet wait. It is a late-response sequencing bug: a request opens the loading gate after scene dispatch has closed, then its response cannot reach the clearing parser.
- changed:
  - `mock_scene_change_combo_response()` now pre-bundles the WT44 task/other/banner subset for the observed second-map request shape, before trailing `30/1`.
  - to stay within the confirmed practical 10-object limit, that combo keeps `30/2`, `27/12`, `27/4`, `7/42`, `6/1`, `6/13`, `6/14`, `2/10`, `25/5`, and `30/1`, and omits the empty `27/11` object only in this specific experiment.
- validation:
  - `make` succeeds after the builder change.
- next verification:
  - rerun manually and check the scene-change marker:
    - expected: `mock_scene_change_combo_response objects=10 ... fb11empty=0 ... taskSubset=1 ... trailingSceneEnter=1`.
  - then check whether the later `WT len=44` disappears. If it still appears, inspect whether `loadingGate_R9_5530` is cleared before/after its `early_gate_off`.

## 2026-06-09 rerun: pre-bundled WT44 subset is negative

- verified from the newest manual run:
  - the experimental 10-object combo was consumed: `mock_scene_change_combo_response objects=10 ... fb11empty=0 ... taskSubset=1 ... trailingSceneEnter=1`.
  - it did not suppress the later post-scene-change request. The client still sent `WT len=44 hdr=25/5 objs=1/25/5,1/6/1,1/6/13,1/6/14,1/2/10,1/25/5`.
  - the WT44 mock response was queued as `builtin-scene-task-subset-followup responseLen=177`, but its data event entered `net_business_response_dispatch()` with `dispatchGate=0` and exited at `early_gate_off` (`0x01012F42`).
  - after that event, `R9+0x5530` remained `1`, and the loading widget continued through `trace_loading_gif_widget_draw_entry ... flag10=1 gate5530=1` plus `trace_progress_strip_wrapper_tick ... dst=20,188`.
  - only the later timeout path cleared it: `trace_scene_loading_owner_write field=loadingGate_R9_5530 ... pc=01013c6a`, followed by `trace_scene_message_request label=scene_widget_timeout_message ... text=网络连接超时!`.
- conclusion:
  - pre-bundling the parser-safe task/other/banner subset inside the scene-change combo is confirmed negative. It neither suppresses WT44 nor closes the request lifecycle.
  - the next boundary is the local `sub_1013BDC()` wait-callback branch that calls `R9+0x554C`; in the failing run that callback sends WT44 from `0x01013C02..0x01013C05`.
- changed:
  - reverted the 10-object task-subset scene-change experiment so `mock_scene_change_combo_response()` again mirrors the requested scene-change family plus scene-aware trailing `30/1`.
  - added read-only `trace_scene_loading_callback_gate` at `0x01013BDC`, `0x01013C00`, `0x01013C02`, and `0x01013C04` to log `R9+0x5540`, `*(R9+0x5540+0x10)`, `R9+0x554C`, the callback return in `R0`, and the loading gate bytes.
- validation:
  - `make` succeeds after this trace-only/code cleanup change.

## 2026-06-09 rerun: wait callback trace confirms normal first-scene timing

- verified from the newest manual run:
  - the first map's normal resource follow-up is driven by `sub_1013BDC()` calling the wait callback at `R9+0x554C == 0x0103478F`.
  - before the callback, `trace_scene_loading_callback_gate ... waitObj=01056124 waitObjFlag10=7 gate5530=0 gate5531=0`.
  - the callback sends `WT len=49` from `0x01013C05`; after `BLX`, `R0=0x31`, `waitObjFlag10=0`, `gate5530=1`, and `gate5531=1`.
  - that WT49 response dispatches while `sceneObj+0x164` is still open, so handlers run and clear `R9+0x5530`; the normal first scene ends with `sceneState=7 loadFlags=0,0,0`.
- second-map state:
  - the scene-change combo is back to the parser-faithful shape: `mock_scene_change_combo_response objects=6 ... fb11empty=1 ... taskSubset=0 ... trailingSceneEnter=1`.
  - it consumes `30/2`, `27/12`, `27/11`, `27/4`, `7/42`, and `30/1`, restoring `sceneState=7`.
  - immediately after the combo data event, the same callback path sends `WT len=44` from `0x01013C05`; the response reaches `early_gate_off` and leaves `R9+0x5530=1`.
- instrumentation correction:
  - the first version of `trace_scene_loading_callback_gate` spent its 96-line cap on idle `0x01013BDC` entries before the second-map callback. It proved the first-map timing but missed the second-map callback boundary.
  - the trace now skips idle entry logs, raises the cap to 160, and keeps callback-boundary logs.
  - `vm_net_trace_scene_loading_owner_write` now also watches `R9+0x5540`, `R9+0x554C`, and `R9+0x5564` (`waitObj+0x10`) with a 256-line cap, so the next run should identify the parser/site that reactivates the WT44 wait after scene-change completion.
- validation:
  - `make` succeeds after this trace refinement.

## 2026-06-09 rerun: WT44 is built by outgoing event objects after `30/2`

- verified from the newest manual run:
  - IDA shows `R9+0x5564` is not a separate business flag. It is `eventObj+0x10`, the current outgoing WT object count. `event_packet_add_field()` increments it at `0x0103460E`; `event_packet_init()` clears it at `0x01034702`.
  - `sub_1013BDC()` tests that count via `*(*(R9+0x5520+0x20)+0x10)`. If it is nonzero and `R9+0x5531 == 0`, it calls the send callback at `R9+0x554C`.
  - on the second-map scene-change response, `sub_1013BDC()` sends the request with `waitObjFlag10=4` before the combo response dispatches. That request is the scene-change WT89.
  - while the combo is being consumed, the `30/2` handler allocates a new outgoing object: after `trace_business_handler ... kind=30 subtype=2`, `event_packet_add_field()` writes `R9+0x5564` from `0` to `1`.
  - after the combo completes, the object count continues from `1` to `6`, and `sub_1013BDC()` sends `WT len=44` from the same callback path. Its response still reaches `early_gate_off`.
- static evidence:
  - IDA `sub_1049188()` (`0x01049188`) calls `alloc_outgoing_game_event(2, 0, 25, 5)` at `0x01049192`, then clears fields on the active scene/UI object. This matches the first object in WT44 (`1/25/5`).
  - IDA `event_packet_add_field()` (`0x010345D4`) takes the outgoing event object, initializes a slot with `sub_1034518()`, optionally sets byte `+5`, and increments `eventObj+0x10` at `0x0103460E`.
- changed:
  - added read-only `trace_alloc_outgoing_game_event` at `alloc_outgoing_game_event()` entry/return (`0x0100E2E4`, `0x0100E302`) to capture each outgoing object's caller and kind/subtype in the next run.
- hypothesis:
  - `30/2` in the scene-change combo may be the direct cause of the late WT44 follow-up. If the next run confirms all WT44 objects are emitted from `30/2`/scene-change side effects, the next builder experiment should test whether the scene-change response can complete with scene-aware `30/1` plus the requested `27/*`/`7/42` family but without `30/2`.
- validation:
  - `make` succeeds after adding this trace.

## 2026-06-09 rerun: WT44 tail comes from `scene_runtime_tick`

- verified from the newest manual run:
  - the scene-change combo remains parser-valid and scene-aware: `mock_scene_change_combo_response objects=6 ... trailingSceneEnter=1 scene=00蓬莱仙岛_02.sce exitId=1 pos=128,45`.
  - `30/2` is confirmed as the source of only the first late outgoing object: after `trace_business_handler ... kind=30 subtype=2`, `trace_alloc_outgoing_game_event ... lr=01049197 ... regs=...,00000019,00000005`, matching `sub_1049188()` (`0x01049188`) and object `25/5`.
  - after the combo dispatch returns, the outgoing object count continues from `1` to `6`; the final traced objects are `2/10` from caller `0x0100EDA0` and `25/5` from caller `0x0100ED8E`.
  - the resulting request is still `WT len=44 hdr=25/5 objs=1/25/5,1/6/1,1/6/13,1/6/14,1/2/10,1/25/5 count=6`, and its response still reaches `early_gate_off` at `0x01012F42`.
- static evidence:
  - IDA `scene_runtime_tick()` (`0x01014E54..0x01014EEE`) gates a one-shot sync on `[R4+1] == 1`, directly queues `6/1`, `6/13`, and `6/14`, clears `[R4+1]` at `0x01014EE6`, then calls `send_game_event_type(101)` (`0x0100ED90`, `2/10`) and `send_default_scene_event()` (`0x0100ED80`, `25/5`).
  - therefore `30/2` is not the whole WT44 source. Removing only `30/2` is still a hypothesis and may merely change the late request shape instead of preventing it.
- changed:
  - expanded read-only `trace_scene_runtime_tick` to log the real `R4` tick-state pointer, `[R4+0..4]`, `R6+0x0D`, and the existing scene/loading fields.
  - added trace points at `0x01014E54` (`oneshot_sync_check`) and `0x01014EE6` (`oneshot_sync_clear`) to observe the one-shot sync branch without patching it.
- validation:
  - `make` succeeds after the trace-only change.
- next verification:
  - rerun manually and check `trace_scene_runtime_tick label=oneshot_sync_check/oneshot_sync_clear`.
  - if `[R4+1]` is set only after a specific scene-change handler, identify that handler before changing builders. If it is expected normal behavior, the server-side fix likely needs a sequencing/response-shape change that lets the WT44 response dispatch before the business gate closes, not another forced local clear.

## 2026-06-09 rerun: second-map WT44 still late; one-shot trace cap corrected

- verified from the newest manual run:
  - this run used the rebuilt trace binary (`net_trace.log` newer than `bin/main.exe`) and again reproduced the stuck strip.
  - the second-map scene-change combo is unchanged and parser-consumed:
    - `mock_scene_change_combo_response objects=6 ... trailingSceneEnter=1 scene=00蓬莱仙岛_02.sce exitId=1 pos=128,45`
    - dispatch order: `30/2`, `27/12`, `27/11`, `27/4`, `7/42`, `30/1`.
  - after `30/2`, `sub_1049188()` creates the first outgoing `25/5`; after the combo finishes, `eventObj+0x10` increments through the remaining one-shot sync objects and `sub_1013BDC()` sends `WT len=44`.
  - the WT44 response still reaches `net_business_response_dispatch()` with `dispatchGate=0` and exits at `early_gate_off` (`0x01012F42`), leaving `R9+0x5530=1` until the timeout branch clears it at `0x01013C6A`.
- instrumentation correction:
  - the first `oneshot_sync_*` trace captured the normal first-map one-shot branch at tick 255, but its cap was exhausted before the second-map branch around tick 472.
  - `R4` in `scene_runtime_tick()` is confirmed as `R9+0x5C64`; the branch gate `[R4+1]` is therefore `R9+0x5C65`.
- changed:
  - expanded `vm_net_trace_scene_loading_owner_write()` to watch `R9+0x5C64`, `R9+0x5C65`, and `R9+0x5C66`.
  - filtered empty `oneshot_sync_check` entries and raised the `trace_scene_runtime_tick` cap so the next run should preserve the second-map one-shot boundary.
- validation:
  - `make` succeeds after the trace-only change.
- next verification:
  - rerun manually and inspect `trace_scene_loading_owner_write field=sceneTickOneShot_R9_5C65` near the second-map transition.
  - that write PC should identify whether the one-shot WT44 is caused by `30/2`, `27/12`, `27/4`, `30/1`, or scene-runtime init outside the packet parser.

## 2026-06-09 rerun: one-shot WT44 is scene-runtime init, not a parser side effect

- verified from the newest manual run:
  - `sceneTickOneShot_R9_5C65` is set on both first-map and second-map scene initialization by `scene_runtime_init_and_sync()` at `0x01013010`.
  - the second-map write occurs before the scene-change response is dispatched: `trace_scene_loading_owner_write field=sceneTickOneShot_R9_5C65 ... pc=01013010 lr=01039d1b tick=325`.
  - IDA confirms `0x01013010` is the fixed initialization write `STRB #1, [R4,#1]`, with `R4 = R9+0x5C64`; it is not a field-specific packet parser side effect.
  - the later WT44 is therefore the normal scene-runtime one-shot sync family. The failure is sequencing: the current scene-change combo closes the business dispatch gate before that one-shot response arrives.
- changed:
  - for the observed portal scene-change request shape (`2/3` plus `27/11`, `27/4`, `7/42`, no `12/1`), `mock_scene_change_combo_response()` now defers gate-closing scene completion.
  - that combo keeps only the non-closing requested family (`27/11`, `27/4`, `7/42`) and records the requested scene/exit target.
  - `mock_scene_task_subset_followup_response()` now accepts the one-shot task subset by object shape instead of fixed length, then appends the deferred scene completion objects while the gate should still be open:
    - parser-safe `6/1`, `6/13`, `6/14`, `2/10`, `25/5`
    - scene-aware `27/12`, `27/11`, `27/4`, trailing `30/1`
- validation:
  - `make` succeeds after the builder sequencing experiment.
- next verification:
  - rerun manually and check for `mock_scene_change_combo_response ... deferredSceneCompletion=1 trailingSceneEnter=0`.
  - expected follow-up is `mock_scene_task_subset_followup_response ... deferredSceneCompletion=1 trailingSceneEnter=1 lateDispatchExpected=0`.
  - success criteria: the WT44 response enters `net_business_response_dispatch()` with `dispatchGate=1`, consumes the deferred `30/1`, and no longer leaves `R9+0x5530=1` / timeout strip active.

## 2026-06-09 correction: fully deferring `30/2` causes map bounce-back

- user-observed result:
  - after entering the next map, the client immediately returns to the previous map.
- verified from logs:
  - the experimental scene-change combo had only three objects and omitted `30/2`: `mock_scene_change_combo_response objects=3 ... trailingSceneEnter=0 deferredSceneCompletion=1 scene=00蓬莱仙岛_02.sce`.
  - the client then consumed only `27/11`, `27/4`, and `7/42`, leaving `sceneState=0` after that response.
  - the one-shot subset response later consumed a trailing `30/1`, but the client then emitted a new scene-change request back to `c00蓬莱仙岛_01`, matching the visual bounce-back.
- conclusion:
  - fully deferring `30/2` is confirmed negative. `30/2` is needed early to commit the requested scene-change target.
- changed:
  - restored `30/2` inside `mock_scene_change_combo_response()` for the portal scene-change path.
  - kept the narrower experiment of deferring later scene-completion tail objects (`27/12` / trailing `30/1`) into the one-shot subset response.
- validation:
  - `make` succeeds after this correction.
- next verification:
  - rerun manually and confirm `mock_scene_change_combo_response ... sceneChangeResult=1 ... deferredSceneCompletion=1`.
  - if bounce-back is gone but the progress strip returns, compare whether `30/2` alone closes the gate early enough to make the one-shot subset response hit `early_gate_off`.

## 2026-06-09 correction: early `30/2` prevents bounce but still closes the gate

- user-observed result:
  - restoring early `30/2` removes the map bounce-back, but the progress strip returns.
- verified from logs:
  - the scene-change combo marker is `mock_scene_change_combo_response objects=4 ... sceneChangeResult=1 ... deferredSceneCompletion=1`.
  - `30/2` immediately closes the business gate at `trace_scene_dispatch_gate_write ... pc=0103980e`.
  - the one-shot subset response is built as deferred completion (`mock_scene_task_subset_followup_response ... deferredSceneCompletion=1 ... len=369`) but still reaches `early_gate_off` with `dispatchGate=0`.
- conclusion:
  - early `30/2` is sufficient to prevent bounce-back but too early for the one-shot WT44 timing.
- changed:
  - replaced the deferred `30/1` experiment with a narrower deferred `30/2` experiment:
    - scene-change combo again omits `30/2`, leaving the business gate open.
    - one-shot subset response appends the remembered scene-change `30/2 scene+posinfo` as its final object.
  - rationale: `30/2` is the confirmed scene-change commit path and also clears `R9+0x5530`; putting it at the end of the one-shot response should allow that response to dispatch before the gate closes.
- validation:
  - `make` succeeds after this builder-only change.
- next verification:
  - rerun manually and check:
    - `mock_scene_change_combo_response ... sceneChangeResult=0 ... deferredSceneCompletion=1`
    - `mock_scene_task_subset_followup_response ... deferredSceneChangeResult=1`
    - the WT44 response dispatches with `dispatchGate=1`, consumes final `30/2`, and does not bounce back or leave `gate5530=1`.

## 2026-06-09 correction: deferred `30/2` also bounces back

- user-observed result:
  - deferring `30/2` into the one-shot response again makes the client enter the next map and immediately return to the previous map.
- verified from logs:
  - the scene-change combo was marked `sceneChangeResult=0 ... deferredSceneCompletion=1`.
  - after the one-shot response, the client emitted another scene-change request back to `c00蓬莱仙岛_01`.
- conclusion:
  - both variants that omit `30/2` from the immediate scene-change combo are confirmed negative. The client needs early `30/2` to commit the requested target map before later runtime ticks.
  - the remaining progress-strip issue should not be solved by moving scene completion into WT44.
- changed:
  - reverted the deferred scene-completion experiments.
  - `mock_scene_change_combo_response()` is back to the complete early scene-change response.
  - `mock_scene_task_subset_followup_response()` is back to parser-safe task/other/banner objects only.
- validation:
  - `make` succeeds after reverting the experiment.

## 2026-06-09 experiment: split `30/2` scene ack from `posinfo`

- checked latest submitted logs:
  - `bin/logs/net_trace.log` and `net_packets.log` were written at `09:56`, before the rebuilt `bin/main.exe` at `09:57`.
  - those logs still show the old negative experiment marker: `mock_scene_change_combo_response ... sceneChangeResult=0 ... deferredSceneCompletion=1`.
  - therefore they confirm the already-known bounce-back failure when early `30/2` is omitted, but they do not test the restored/new binary.
- new static evidence:
  - IDA `parse_scene_posinfo_field()` (`0x01039770`) reads `result`, `scene`, then optional `posinfo`.
  - when `result==1` and `posinfo` exists, it applies the scene/position and closes the scene business dispatch gate by writing `sceneObj+0x164=0` at `0x0103980E`.
  - regardless of `posinfo`, it still calls `sub_10491AE()` at `0x0103993C`; that path calls `sub_1049188()` and creates the follow-up `25/5` event.
  - IDA `ui_apply_named_posinfo_target()` (`0x0100E9B8`) used by `27/12` also closes the same gate at `0x0100EA6A`, so `27/12` must not remain in the early scene-change combo for this experiment.
- changed:
  - added `vm_net_mock_put_scene_ack_without_posinfo()` for a `30/2` object containing only `result`, `type`, and `scene`.
  - `mock_scene_change_combo_response()` now sends that early `30/2` ack without `posinfo`, then mirrors the requested non-closing follow-up objects (`27/11`, `27/4`, `7/42`).
  - `mock_scene_task_subset_followup_response()` now appends the deferred scene-position completion while the gate should still be open: `27/12`, `27/11`, `27/4`, then full `30/2 result/type/scene/posinfo`.
- status:
  - hypothesis under test: early `30/2` without `posinfo` may be enough to avoid bounce-back while keeping the business gate open for the normal WT44 one-shot response.
- validation:
  - `make` succeeds after this builder-only experiment.
- next verification:
  - rerun manually with the rebuilt binary and check for:
    - `mock_scene_change_combo_response ... sceneChangeResult=1 trailingSceneEnter=0 deferredSceneCompletion=1`
    - `mock_scene_task_subset_followup_response ... deferredSceneChangeResult=1 deferredSceneCompletion=1 lateDispatchExpected=0`
    - no immediate request back to `c00蓬莱仙岛_01`
    - the WT44 response entering business dispatch instead of `early_gate_off`.

## 2026-06-09 rerun: split `30/2` works, empty `25/5` reentrant flush is suspect

- verified from the newest manual run:
  - the split scene-change experiment is confirmed positive for the previous loading strip:
    - `mock_scene_change_combo_response objects=4 ... sceneChangeResult=1 trailingSceneEnter=0 deferredSceneCompletion=1`
    - the following WT44 response is not blocked by the scene dispatch gate: `trace_business_dispatch_state label=entry ... dispatchGate=1 objectCount=9`
    - the deferred full `30/2` is consumed at index 8, closes `sceneObj+0x164` at `0x0103980E`, and clears `R9+0x5530` at `0x010491A8` / `0x01039946`.
  - this disproves the older hypothesis that the active strip must wait for timeout in this shape. The relevant gate after WT44 is `R9+0x5531`, not `R9+0x5530`.
  - immediately after WT44 completion, `sub_1013BDC()` sends a short empty `WT len=9 hdr=25/5`. The mock replies with `builtin-scene-default-event responseLen=23`, and because `vm_net_mock_on_send()` immediately flushes short `25/5`, the response is dispatched while the client's send wrapper is still on the stack:
    - `queue_data_event_immediate_flush ... tick=438`
    - data event hits `early_gate_off` with `dispatchGate=0`, then clears `R9+0x5531` at `0x010348F6`
    - after returning to the send wrapper, `0x010347E8` writes `R9+0x5531` back to `1`
  - later local movement requests continue to append `2/1 moveinfo` objects until the outgoing WT object count reaches the cap (`eventObj+0x10 == 10`). At tick 1042 the next scene init cannot enqueue its `2/3`, `27/11`, `27/4`, or `7/42` requests because the same object queue is full.
- changed:
  - kept the parser-safe `25/5 -> 25/12 result=4` response, but removed the special same-stack immediate flush for short `25/5`.
  - hypothesis: delivering that response asynchronously lets the client send wrapper set `R9+0x5531` first, then lets the normal network callback clear it afterward, avoiding the stale gate and queue fill.
- validation:
  - rebuild required after this source change.
- next verification:
  - rerun manually and check that `builtin-scene-default-event` no longer logs `queue_data_event_immediate_flush`.
  - success marker: after the short `25/5` response, `R9+0x5531` should return to `0`, and later movement should not fill `eventObj+0x10` to cap 10 before the next scene-enter request.

## 2026-06-09 rerun: async short `25/5` fixes queue-cap blocker

- verified from the newest manual run:
  - `builtin-scene-default-event` responses are still built as `25/12 result=4`, but no `queue_data_event_immediate_flush` marker appears.
  - the short `25/5` response is queued and fired on the next scheduler turn, e.g. tick `4655` send / tick `4656` fire and tick `4866` send / tick `4867` fire.
  - after those responses, `runtime_state` stays `sceneState=7` with `loadFlags=0,0,0`; no `scene_widget_timeout_message`, `unhandled_packet`, or `assert(0)` marker appears.
  - the prior queue-cap failure is gone. The client successfully sends later scene-enter requests:
    - tick `4651`: `trace_scene_send_map_enter_request`, then `2/3 mapID=c00蓬莱仙岛_03.sce exitID=0`.
    - tick `4862`: another `trace_scene_send_map_enter_request`, then `2/3 mapID=00蓬莱仙岛_02.sce exitID=0`.
  - grep found no `count=10 cap=10` in the new run.
  - remaining `early_gate_off` entries after movement are the known `2/1 moveinfo` empty acks and `2/10` refresh acks arriving after scene dispatch has closed; they clear the wrapper gate and do not block subsequent scene-change sends in this run.
- conclusion:
  - confirmed: same-stack immediate flush for short `25/5` was a mock network timing bug.
  - confirmed partial: split `30/2` plus deferred full `30/2` handles repeated portal scene changes without the earlier stuck loading strip or queue-cap failure.
- next check:
  - visually confirm whether any user-visible progress widget persists after the map title prompt. Current trace only shows transition-local progress ticks; steady `loading_gif_widget_draw_entry` lines have `flag10=0 gate5530=0`.

## 2026-06-09 rerun: unhandled short `7/18`

- verified from the newest manual run:
  - previous loading/queue-cap issue remains fixed, but a new HUD/touch action emits an unhandled short request:
    - `WT len=9 hdr=7/18 objs=1/7/18 count=1`
    - packet log ends with `unhandled_packet` / `assert(0)`.
  - runtime source is confirmed by trace:
    - `trace_alloc_outgoing_game_event ... lr=0102c301 ... regs=0000000a,00000001,00000007,00000012`
    - IDA `sub_102C2E8` sends `alloc_outgoing_game_event(10,1,7,18)` at `0x0102C2FE`.
  - static parser evidence:
    - `net_handle_misc_player_fields()` (`0x01011C88`) does not handle subtype `18`.
    - nearby UI-local parser `sub_102C104` handles kind `7` subtype `17`, `34`, and `4`.
    - for subtype `17`, `sub_102C104` reads `result`, `useinfo`, then `pcimg`; `result == 1` shows the `useinfo` text, while failure shows local `网络异常!`.
- changed:
  - added `builtin-item-use18`, which answers short `7/18` as `1/7/17 { result=1, useinfo="OK", pcimg=0 }`.
- status:
  - field order is confirmed by IDA.
  - end-to-end consumption is still hypothesis because the failing trace had `sceneObj+0x164 == 0`; a normal business response may still reach `early_gate_off`. The next run must verify whether `trace_business_handler` reaches the `7/17` UI parser path or whether the scene dispatch gate remains the blocker.
- validation:
  - rebuild required after this source change.

## 2026-06-09 superseded hypothesis: map-switch progress fill as signed LCD rectangles

- user-observed problem:
  - the full-screen map-switch loading panel advances to the next scene, but the visible progress frame stays empty/0% for the whole load.
- evidence:
  - runtime trace shows transition-local progress fill calls while scene loading is active: `lcd_shape api=vMFillRectEx x=197 y=41/48/.../97 w=6 h=-13 color=000000c4 last=010034fc`.
  - IDA maps `last=0x010034FC` into `sub_100337C()`, where the game calls the LCD manager `+0x4C` fill-rect slot.
  - initial interpretation treated `h=-13` as an intentional negative-height rectangle and current emulator clipping as the draw blocker.
- changed:
  - added signed-rectangle normalization for LCD rectangle clipping in `src/main.c`.
  - `vMDrawRect`, `vMDrawRectEx`, `vMFillRect`, and `vMFillRectEx` now normalize negative width/height before clipping and drawing.
- status:
  - superseded by the next entry: the `h=-13` trace was a parameter-shift artifact from decoding a 5-argument `FillRect` as `FillRectEx`.

## 2026-06-09 correction: loading progress fill was a 5-arg FillRect call

- new evidence from the next rerun:
  - the prior `lcd_shape api=vMFillRectEx x=197 y=41/48/... w=6 h=-13 color=000000c4 last=010034fc` trace does not match the visible horizontal progress frame.
  - IDA `sub_100337C()` shows `0x010034FC` calls the picture/resource table fill helper with logical arguments `x=33`, `y=n210+9`, `w=sub_104D538(max,174*counter)`, `h=imageHeight-18`, `color=0xFFF3`.
  - therefore the earlier trace was a parameter-shift artifact: the emulator treated `R0=33` as a destination image pointer for `FillRectEx`, so the real `y=197` became logged as `x=197`, the real `w=progress` became logged as `y`, and the real color became logged as `h=-13`.
- changed:
  - added a `vMFillRectEx` compatibility path in `src/main.c`: if `R0` is a plausible coordinate rather than an image pointer, decode the call as 5-argument `FillRect(x,y,w,h,color)`.
  - kept signed-rectangle normalization as a general clipping helper, but the loading progress issue is no longer classified as confirmed negative-height drawing.
- expected next trace:
  - `last=010034fc` should now appear as `lcd_shape api=vMFillRectCompat x=33 y=197 w=<increasing> h=6 color=0000fff3`.
- validation:
  - rebuild required, then rerun manually and confirm the full-screen loading progress bar fills horizontally.

## 2026-06-09 rerun: `7/18` handled but `7/17` response is gate-blocked

- verified from the newest manual run:
  - the previous `WT len=9 hdr=7/18` unhandled/assert path is gone. Packet log now shows `source=builtin-item-use18 responseLen=48`.
  - the mock response is the intended `1/7/17` shape: trace logs `mock_item_use18_response response=7/17 result=1 useinfo=OK pcimg=0 len=48`.
  - the request opens the loading owner at `0x01034796`: `trace_scene_loading_owner_write field=loadingGate_R9_5530 ... old=0 new=1`.
  - the queued response fires one tick later, but `net_business_response_dispatch()` sees `dispatchGate=0` and exits at `early_gate_off` (`0x01012F42`) before unpack/object iteration.
  - wrapper cleanup clears only `R9+0x5531` at `0x010348F6`; `R9+0x5530` remains `1`, and the loading strip continues with `trace_loading_gif_widget_draw_entry ... flag10=1 gate5530=1`.
- conclusion:
  - `7/18` is now handled at the mock packet level.
  - `7/17` field order remains confirmed by IDA `sub_102C104`, but end-to-end consumption is not confirmed. The current blocker is response delivery while the scene business dispatch gate is closed.
- changed:
  - added read-only trace fields to `trace_business_dispatch_state`: `fallbackCb=R9+0x5D30`, `manager=R9+0x5EF0`, `managerChild`, and `managerChildCb10`.
- validation:
  - rebuild required after the trace-only source change.
- next:
  - rerun manually and inspect the gate-blocked `7/17` entry. If `managerChildCb10` resolves to the UI-local `sub_102C104` family, the likely contract is that this response must enter the normal object loop before `sceneObj+0x164` closes. If `fallbackCb` points to a relevant parser, test the alternate delivery contract separately.

## 2026-06-09 rerun: practise-info panel expects `7/18`, not `7/17`

- verified from the newest manual run:
  - the visible screen is `修炼信息`, and tapping it emits the same short `WT len=9 hdr=7/18`.
  - `builtin-item-use18` still replies, but the event enters `net_business_response_dispatch()` with `dispatchGate=0` and takes `early_gate_off`.
  - the new trace shows the active manager child callback at that moment is `managerChildCb10=0102cb47`.
  - IDA maps `0x0102CB47` to `sub_102CB46`, which handles kind `7` subtype `18`.
- static correction:
  - `sub_102CB46` reads `todaypasthour`, `todaypastmin`, `getexp`, `todaylasthour`, `todaylastmin`, `alllasthour`, `alllastmin`, and `isgold`.
  - the previous `7/17 result/useinfo/pcimg` response matches `sub_102C104`, but that is not the active parser for the current `修炼信息` panel.
- changed:
  - renamed the mock path to `builtin-practise-info18`.
  - changed the response object to kind `7` subtype `18` with the confirmed practise-info fields.
  - added a narrow transport experiment: this one short response is queued as event `6` so it can exercise the existing fallback path instead of the scene-gated event `7` object loop.
  - added read-only `trace_practise_info_parser` at `0x0102CB46` and `0x0102CC0E`.
- validation:
  - rebuild required.
- next:
  - rerun manually. If `trace_practise_info_parser ... kind=7 subtype=18` appears and `R9+0x5530` clears, the field correction plus fallback-channel hypothesis is confirmed partial. If not, fallback event `6` is a confirmed negative and the remaining problem is still dispatch ownership/gate sequencing.

## 2026-06-09 rerun: fallback event `6` does not reach practise parser

- verified from the newest manual run:
  - the mock now builds the corrected practise response: `source=builtin-practise-info18 responseLen=151 WT len=9 hdr=7/18 objs=1/7/18 count=1`.
  - packet bytes contain the `sub_102CB46` field order: `todaypasthour`, `todaypastmin`, `getexp`, `todaylasthour`, `todaylastmin`, `alllasthour`, `alllastmin`, `isgold`.
  - the transport experiment queued that response as event `6`: `queue_data_event ... event=6 len=151`.
  - at tick `165`, `net_business_response_dispatch()` is entered with `r3=6`, `dispatchGate=0`, `fallbackCb=05083027`, `managerChild=01058af8`, and `managerChildCb10=0102cb47`.
  - the dispatcher logs `fallback_exit`, but no `trace_practise_info_parser` entry appears, and `R9+0x5530` remains `1`; the progress strip continues drawing.
- conclusion:
  - confirmed negative: event `6` fallback delivery does not call the active manager-child parser `sub_102CB46`.
  - confirmed: the current blocker is no longer the `7/18` field order. It is response delivery while the normal event `7` object loop is gated off after map entry.
- changed:
  - reverted the `7/18` response back to default event `7` delivery. The corrected `builtin-practise-info18` packet body remains.
- validation:
  - rebuild required after this source change.

## 2026-06-09 experiment: keep first-scene business gate open

- new evidence:
  - `event=6` fallback is confirmed negative, so the corrected `7/18` response must reach the normal event `7` object loop.
  - the first-scene WT49 response closes `sceneObj+0x164` before the later `修炼信息` request:
    - `27/12` closes it at `0x0100EA6A`.
    - trailing `30/1` with `posinfo` writes the already-zero gate at `0x01039766`.
  - IDA confirms those are parser side effects, not draw/UI artifacts:
    - `ui_apply_named_posinfo_target()` (`0x0100E9B8`) closes the gate unconditionally.
    - `parse_scene_response_entry()` (`0x010396EA`) closes the gate when `posinfo` is present.
- changed:
  - added the builder experiment `CBE_FIRST_SCENE_KEEP_BUSINESS_GATE` (default `1`) for the first-scene WT49 response.
  - in that mode, `mock_scene_resource_followup_response` omits the gate-closing `27/12/27/11/27/4` trio and replaces full `30/1 scene+posinfo` with non-closing `30/2 result+scene` without `posinfo`.
  - this is intentionally a server-response experiment, not a client global write or direct parser call.
- next verification:
  - rerun manually and check whether `mock_scene_resource_followup_response ... keepBusinessGate=1` is followed by a later `trace_practise_info_parser` for `7/18`.
  - if map completion regresses or `7/18` still hits `early_gate_off`, rerun with `CBE_FIRST_SCENE_KEEP_BUSINESS_GATE=0` to restore the previous first-scene response while continuing gate/ownership research.
- validation:
  - rebuild required.

## 2026-06-09 rerun: first keep-gate packet was malformed

- verified from the newest manual run:
  - the experiment marker appeared: `mock_scene_resource_followup_response ... trailingSceneEnter=0 keepBusinessGate=1 len=280`.
  - the gate-closing writes at `0x0100EA6A` and `0x01039766` no longer appeared in that WT49 response window.
  - however, `net_business_response_dispatch()` failed at unpack time: `trace_business_dispatch_state label=unpack_error ... r0=00000004 objectCount=7`.
- root cause:
  - the experiment branch wrote the `30/2 result/type/scene` fields directly into the WT stream without wrapping them in a `1/30/2` object header and object-length footer.
  - therefore this run is not evidence against the keep-gate hypothesis; it only exposed a malformed mock packet.
- changed:
  - fixed `mock_scene_resource_followup_response` keep-gate branch to wrap the non-closing `30/2` ack with `vm_net_mock_begin_wt_object(..., 1, 0x1e, 2, ...)` and `vm_net_mock_finish_wt_object(...)`.
- validation:
  - rebuild required.

## 2026-06-09 rerun: practise parser reached, time-field encoding corrected

- verified from the newest manual run:
  - fixed keep-gate WT49 now unpacks successfully: `mock_scene_resource_followup_response ... keepBusinessGate=1 len=286`, followed by `after_unpack_ok`.
  - the response consumes seven objects, including the non-closing `kind=30 subtype=2`.
  - the later `7/18` response enters the active manager-child parser: `trace_practise_info_parser label=entry ... kind=7 subtype=18 managerChildCb10=0102cb47`.
  - the normal business handler then clears `R9+0x5530`; this matches the user-observed progress strip disappearing immediately.
- remaining mismatch:
  - the visible practise panel showed huge time values.
  - trace after fields showed `today=29807:26469` and `all=24940:26995`, proving the prior `u16` object encoding was not accepted by the packet getter used at `sub_102CB46`.
  - `getexp` stayed sane because it was already encoded with the regular integer/u32 helper.
- changed:
  - encoded all practise time fields with `vm_net_mock_put_object_u32()` while keeping the same parser field names.
  - current synthetic values are `today=0:15`, `getexp=120`, `todayLast=1:45`, `allLast=1:45`, `isgold=0`.
- validation:
  - rebuild required.

## 2026-06-09 rerun: crash while entering dynamic game CBM

- verified from the newest manual run:
  - after `dlCbm: enter game 111 cbmName is JHOnlineData\mmGameMstarWqvga.cbm`, the client called `DF_DataPackage_LoadPackage` twice and then crashed with `[vm_malloc] FAILED size=21521016`.
  - storage trace shows the second package load reopened the embedded main package path:
    - `file_open_request ... name=CBE/����OL.CBE hint=rbH...flowerStyle.gif selected=rbwr`
    - `file_seek ... pos=353281 ... result=353281`
    - `file_read ... size=4 result=0`
  - the selected host mode was wrong. The intended guest hint was `rb`, but the hint buffer was not NUL-terminated and the emulator scanned later guest bytes, collecting `w`/`r` from adjacent resource text.
  - `bin/CBE/江湖OL.cbe` was found at zero bytes after the bad writable open; it was restored from the tracked git blob to the expected `834345` bytes.
- conclusion:
  - confirmed root cause: this crash is a VM file-open mode contract bug, not a network packet or scene parser problem.
  - the bogus `21521016` allocation is downstream of a failed package-size read after opening the main CBE package with a corrupted mode string.
- changed:
  - `vm_file_select_mode()` now accepts only a leading `r`/`w`/`a` token plus optional `b`/`+`, and stops at the first non-mode byte.
  - `vm_DF_DataPackage_LoadFormTCardEx()` now writes the full NUL-terminated `"rb"` hint into guest memory before calling `vm_cbfs_vm_file_open`.
- next verification:
  - rebuild and rerun manually. Expected trace is `selected=rb` for `CBE/江湖OL.CBE`, followed by a successful 4-byte package-size read at offset `353281` and no `vm_malloc` assertion.

## 2026-06-09 change: persist mock player map position

- problem:
  - after relaunching/re-entering the game, the mock always rebuilt `actorinfo` / scene `posinfo` from fixed defaults (`c00蓬莱仙岛_01`, `223,382`), so the player returned to the initial spawn point.
- evidence:
  - runtime logs show normal walking sends `WT len=32 hdr=2/1 objs=1/2/1` with field `moveinfo`.
  - packet dumps show `moveinfo` is a 10-byte compact stream (`03 03 03 ...`, `01 01 ...`, etc.), not an absolute `posx/posy` pair.
  - existing trace helpers confirm the current local player node already carries applied coordinates at `currentNode+0x18/+0x1A` and target coordinates at `+0x11E/+0x120`.
- changed:
  - added a mock-server position state file `nvram/jhol_mock_player_pos.bin`.
  - on `2/1 moveinfo` upload, the mock now reads the current scene node read-only and saves `scene,x,y` as server-side last position.
  - scene-change responses also save the selected target scene/spawn from `mapID/exitID`.
  - login/scene-entry builders now reuse the saved scene/position for `actorinfo`, scene fields, and `posinfo`, while `CBE_SCENE_KEY`, `CBE_SCENE_POS_X`, and `CBE_SCENE_POS_Y` remain override knobs.
- status:
  - confirmed: current mock now has a persistent location source instead of hardcoded spawn-only state.
  - hypothesis: live-server position persistence timing is approximated by saving the already-applied client node position when the movement upload arrives.
- validation:
  - `make` passed after the source change.

## 2026-06-09 rerun: position persistence polluted scene resource key

- verified from the newest manual run:
  - after relaunch, the client entered an unwanted `18/7 type=6` update loop for scene key `c00蓬莱仙岛_01`.
  - storage trace showed the local resource path had degraded to raw `c00...` bytes and was rejected/open-failed, then renamed from `MMORPGTempbin` into the malformed scene-key cache path.
  - this happened immediately after the position persistence change, so the likely regression was feeding a saved best-effort decoded scene string back into `actorinfo` / scene fields.
- conclusion:
  - confirmed: saving coordinates is still useful, but arbitrary guest-label scene text is not safe to persist and replay as a wire/resource key.
- changed:
  - added a whitelist/normalization check for persisted scene keys.
  - old persisted files with bad scene keys are accepted for coordinates, but their scene field is ignored and replaced in memory with the default canonical scene key.
  - move-upload snapshots therefore keep last position without poisoning the resource-update path.
- validation:
  - `make` passed after the fix.

## 2026-06-09 new unhandled `4/1` challenge/menu request

- verified from the newest manual run:
  - packet tail ends with `WT len=60 hdr=4/1 objs=1/4/1(id=105) count=1`.
  - decoded fields are `id=105`, `index=1`, `posx=142`, `posy=310`.
  - the request follows scene/menu touch dispatch on the settled map screen; repeated loading-widget entries in the same tail have `flag10=0 gate5530=0`.
- static evidence:
  - `sub_1037ED4()` (`0x01037ED4`) builds a `1/4/1` request and writes `id`, `index`, `posx`, `posy`.
  - `net_business_response_dispatch()` (`0x01012E4C`) dispatches kind `4` responses to `net_handle_login_or_name_result()` (`0x0101258A`).
  - `net_handle_login_or_name_result()` handles `4/14` by reading only `result`; values `2..7` select local duel/challenge failure messages. It does not implement a confirmed `4/1` response branch.
- changed:
  - added `builtin-challenge-interaction` for the exact single-object `1/4/1 id/index/posx/posy` request.
  - current response is `1/4/14 { result=4 }`, a parser-safe failure response. This avoids guessing the success/battle-start contract and does not write CBE globals.
- validation:
  - rebuild required.

## 2026-06-09 rerun: `4/1` packet handled, parser consumption still gated

- verified from the newest manual run:
  - the previous `4/1` unhandled/assert path is gone:
    - `mock_challenge_interaction_response response=4/14 result=4 len=23`
    - `source=builtin-challenge-interaction responseLen=23 WT len=60 hdr=4/1 objs=1/4/1(id=105) count=1`
  - no `unhandled_packet`, `assert(0)`, `scene_widget_timeout_message`, `count=10 cap=10`, or `queue_data_event_immediate_flush` marker appears.
- caveat:
  - the response fires while the scene business gate is already closed: `runtime_state label=pre_data_event ... sceneGate=0` at tick `893`.
  - no `trace_business_dispatch_item ... kind=4` line appears for this response window; the business-dispatch trace cap is also already exhausted by then, so this is strong but not perfect evidence that `4/14` did not enter `net_handle_login_or_name_result()` on this run.
  - the request briefly opens the local loading owner (`flag10=1 gate5530=1` ticks `892..897`), and the next `2/1 moveinfo` path clears `gate5530` at tick `898`.
- conclusion:
  - confirmed: packet-layer handling for `4/1` is fixed.
  - not confirmed: the final success/failure challenge contract. The current response remains a parser-shaped failure placeholder, not proof of the real battle-start protocol.
