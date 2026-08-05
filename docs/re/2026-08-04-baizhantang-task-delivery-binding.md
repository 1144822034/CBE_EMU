# 白展堂“初来乍到”交付路径缺失（2026-08-04）

Date: 2026-08-04

Status: implemented; awaiting user manual verification

## Trigger and expected flow

The character `guest00001/10001` has successfully accepted task 1, “初来乍到”,
from dynamic NPC Actor `30001` “大侠郭靖” in
`c00蓬莱仙岛_01.sce`.  Talking to Actor `30000` “白展堂” should expose task
1’s submit action and then use the normal client flow:

```text
26/1 NPC dialog (action=4, task=1) -> 6/10 task detail/state request
-> 25/5 completion acknowledgement + 6/4 {result=1, taskdes, awardinfo}
```

The client parser evidence is `ParseNPCDialogData` (`江湖OL.CBE:0x010380E8`),
which creates an actionable entry only from the `dialog` option with `action=4`.
`task_hall_activate_selected_entry` (`0x010492B0`) dispatches that entry through
the existing task request path.  `net_handle_task_response_dispatch`
(`0x0104726C`) handles the subsequent task state and commit responses; `6/6`
is its parser-backed active-to-submittable state notification.

## Runtime and database evidence

The same runtime sequence first proved that the accept path succeeded:

```text
mock_npc_dialog actor=30001 ... name=大侠郭靖 ... task_offer=1 ... task_option_action=4
mock_task action=accept task=1 ... request_subtype=11 response_subtype=11 result=0
```

But both later conversations with white’s actor returned no task option:

```text
mock_npc_dialog actor=30000 ... name=白展堂 ... task_offer=0
task_accepted=0 task_state=0 task_option_action=0 ... objects=1
```

Read-only MySQL evidence records the active task as submittable:

```text
account_role_tasks: role_id=10001, task_id=1, task_state=2, progress1=0, progress2=0
server_dynamic_npc_tasks:
  actor 30001 “大侠郭靖” -> task 1
  actor 30000 “白展堂”   -> task 2
```

Task 1 and task 2 are original `task.dsh` rows (there is no `server_tasks`
override): task 1 is published by “大侠郭靖” and received by “白展堂”; task 2 is
the next task published by “白展堂”.  Therefore the database state and dynamic
NPC bindings are not contradictory: one NPC can publish task 2 while also being
the recipient for task 1.

## First contract violation and root cause

`vm_net_mock_build_npc_dialog_response()` currently derives the dialog’s task
state only from `matchedSeed->taskId`: it treats the clicked NPC’s dynamic
binding as both the offer source and the delivery source.  Actor 30000 is bound
to task 2, so it looks up task 2’s state and never scans the active task 1 row.
The first incorrect state is consequently an empty `optionTasks[]` while task 1
is already state 2 and its definition names this exact NPC as receiver.

The same direct-binding branch also transitions a state-1 task to state 2
without confirming that the clicked NPC is that task’s receiver.  A publisher
must not be able to complete a task merely because it owns the offer binding.

## Correct server responsibility

Dynamic binding (`server_dynamic_npc_tasks`) remains the authority for *offers*.
Delivery is instead determined per active task from its task definition’s
receiver, using the existing
`vm_net_mock_task_prompt_receiver_for_scene()` resolver so XSE-backed current
scene recipient data and the task’s declared receiver stay consistent with the
scene prompt path.

The implementation must:

1. Scan only persisted task states `1` and `2` for the active role.
2. Match the resolved recipient exactly to the clicked scene NPC display name.
3. Move state `1` to `2` only after its two stored requirement counts satisfy
   the task thresholds, then append the existing `6/6` state object.
4. Add a `提交<任务名>` `action=4` option only for state `2` at the matched
   recipient.
5. Restrict the old direct dynamic-binding completion/submission branch to the
   same recipient test, while retaining its state-0 offer behavior.

This does not forge a completion, bypass a prerequisite, or change the client.
It only supplies the existing dialog and state packets to the NPC that task data
already identifies as its delivery owner.

## Verification plan

After build, the user should manually test:

1. With task 1 at state 1, speak to white; the task becomes submittable only if
   all task requirements are complete, and the dialog exposes `提交初来乍到`.
2. Select the submit option and confirm the existing `6/10 -> 25/5 + 6/4`
   commit flow, reward and task state 3.
3. Speak to Guo Jing while task 1 is state 1: it must not move the task to
   state 2 or offer a submit action.
4. Verify that task 2 remains unavailable until task 1’s persisted state is 3.

## Implementation and build

- `src/server/mock_server_scene_sync.c` now has
  `vm_net_mock_task_delivery_matches_scene_npc()`.  It uses the same
  per-scene receiver resolver as the task-prompt builder, so it neither
  hardcodes white’s actor ID nor treats all NPC task bindings as delivery
  bindings.
- The NPC dialog builder scans only the active role’s state-1/state-2 task
  rows.  A matching recipient with satisfied requirements receives the
  existing `6/6` state update and the regular `action=4` submit entry.  A
  state-2 row receives only the submit entry.
- The old direct dynamic-binding path now permits state transition and submit
  only when that same NPC is the resolved receiver; state-0 offer handling is
  unchanged.  Its dialog text no longer overwrites an earlier delivery dialog
  merely because this NPC publishes an unavailable later task.
- `make -j2` completed successfully on 2026-08-04.  No client or server
  process was started, stopped or controlled during validation.
