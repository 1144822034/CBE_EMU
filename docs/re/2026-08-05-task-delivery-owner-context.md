# “关于任务”可在错误 NPC 提交（2026-08-05）

Date: 2026-08-05

Status: implemented; final link/runtime verification blocked by active server

## Trigger and expected behaviour

Task 2 “关于任务” is accepted from 白展堂 and its authoritative `task.dsh`
definition names 郭芙蓉 as receiver. After the task reaches state `2`
(submittable), talking to 白展堂 nevertheless exposed a submit action and the
normal `1/6/4` commit path granted the reward.

Expected ownership is per task definition:

```text
task 2: giver=白展堂, receiver=郭芙蓉
白展堂: may offer task 2; may not deliver task 2
郭芙蓉: may deliver task 2 when its persisted state is 2
```

The user report is the runtime trigger. The present `bin/server_out.txt` does
not contain that interaction yet, so no packet bytes are claimed for this
specific occurrence. Historical data evidence is preserved in
`docs/re/2026-08-03-tongquetai-baizhantang-task-offer.md`, whose parsed real
`task.dsh` table records task 2’s receiver as 郭芙蓉.

## Protocol and parser contract

`ParseNPCDialogData` in `江湖OL.CBE` at `0x010380E8` creates the visible NPC
entry from the server’s `26/1.dialog` sequence. Its `action=4` task option is
then consumed by `task_hall_activate_selected_entry` at `0x010492B0` and the
client sends the normal task detail / submit flow. The final submit request is
`1/6/4 {taskid}`; it contains no actor or NPC identity. The response dispatcher
`net_handle_task_response_dispatch` at `0x0104726C`, case 4, treats `result=1`
as a successful task commit.

Therefore the server must establish the authoritative delivery owner while it
constructs the preceding `26/1` option. It cannot infer an NPC from the final
`6/4` packet alone.

## First violation and root cause

`vm_net_mock_task_delivery_matches_scene_npc()` currently calls
`vm_net_mock_task_prompt_receiver_for_scene()`. That helper first returns the
task definition receiver only if the receiver appears in its *selected* scene
NPC subset. If not, it falls back to any selected seed that has the same
`taskId`, including the dynamic NPC which offers that task.

This is invalid for delivery. The dialog builder itself resolves the clicked
actor from the full catalog (`vm_net_mock_collect_scene_npcinfo_seeds`), while
the prompt helper uses the slot-limited selected catalog
(`vm_net_mock_select_scene_npcinfo_seeds`). Thus a selected offer NPC may
replace a valid, but not selected, named receiver. For task 2 that changes the
delivery comparison from 郭芙蓉 to 白展堂, and the first bad state is an action=4
submit option on 白展堂’s `26/1` response.

The current `6/4` builder compounds this: after receiving only a task ID, it
commits any state-2 task without proving that the preceding option was emitted
by that task’s receiver.

`vm_net_mock_task_definition_is_valid()` requires a non-empty receiver, so a
delivery path has an authoritative receiver string and must not use the
offer/marker fallback.

## Implemented correction

`src/server/mock_server_scene_sync.c` now treats `task->receiver` as the sole
delivery authority. `vm_net_mock_task_prompt_receiver_for_scene()` no longer
substitutes an offer NPC from the selected display subset, and
`vm_net_mock_task_delivery_matches_scene_npc()` compares the declared receiver
directly to the clicked full-catalog NPC name.

The existing per-session task context is now explicitly either `offer` or
`submit`. After a complete `26/1` dialog is built, every actual task option is
recorded with its role, task, actor and scene. A state-2 option creates a
`submit` context. `6/4` consumes that context before task reward/commit can run
for a catalog task. A missing, stale, scene-mismatched or wrong-NPC context
returns the existing failed case-4 result, emits
`mock_task_submit_rejected ... reason=delivery-context`, and does not mutate
the task or grant a reward. The isolated synthetic test-task path remains
unchanged.

## Verification plan

1. Accept task 2 from 白展堂, move it to state 2, and speak to 白展堂: no submit
   option and no `6/4` commit must be available.
2. Speak to 郭芙蓉: exactly one `提交关于任务` action=4 option, followed by the
   normal `6/10 -> 25/5 + 6/4` commit success and state 3.
3. Send a previously observed or manually reproduced direct `6/4` without a
   receiver-bound interaction context: result remains failure; no reward or
   state mutation occurs; the rejection log identifies `delivery-context`.
4. Recheck task 1: it remains deliverable at its declared receiver 白展堂.

## Build result

The modified translation unit compiled successfully under `make -j2` on
2026-08-05. Final linking was blocked because the active process holds
`bin/jh-online-server.exe` and the linker returned `Permission denied` while
opening that output file. No process was stopped, replaced or otherwise
controlled. After the owner stops the active service, rerun `make -j2` before
runtime verification.
