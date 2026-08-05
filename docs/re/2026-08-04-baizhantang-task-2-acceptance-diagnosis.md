# 白展堂任务 2 接取确认与失败原因（2026-08-04）

Date: 2026-08-04

Status: implemented; manual runtime verification pending

## Trigger

The character `guest00001/10001` submitted task 1 “初来乍到” to white’s Actor
`30000` “白展堂”. A later conversation with the same NPC was reported as unable
to accept the next task and gave no reason.

## Runtime evidence and first deviation

The current `bin/server_out.txt` proves that task 2 is not being rejected at the
NPC or task-definition stage:

```text
mock_task_reward task=1 role=10001 ...
mock_task action=commit task=1 ... result=1 ...
mock_npc_dialog actor=30000 ... task_offer=1 ... task_option_action=4 ...
mock_task action=detail task=2 ... request_subtype=10 response_subtype=10
request_state=1 result=0 ...
```

`task_offer=1` means the server has already checked task 2’s level, completion
of prerequisite task 1, and absence of a task-2 state row. The normal `6/10`
detail response has also been sent successfully. There is no later
`WT 1/6/11` request and hence no `6/11 result=1` response in the trace.

The first deviation is therefore *after* the detail response: the client has
not issued its native accept-confirmation request. It is not valid for the
server to pretend that a prerequisite, level, or backpack check failed when
the authoritative offer predicate has just succeeded.

## Client protocol contract

IDA was selected dynamically by `binary_name=江湖OL.CBE`.

1. `task_hall_activate_selected_entry` (`0x010492B0`) receives NPC dialog
   `action=4` and calls `task_hall_request_task_detail` (`0x010491FA`).
2. It sends `WT 1/6/10 {taskid, state=1}` for an offer. The server responds
   with `1/6/10 {info=<GBK task detail string>}`.
3. `net_handle_task_response_dispatch` (`0x0104726C`, case 10) calls
   `ReqTaskInfo` / `SendTaskHallReq` (`0x01038D2C` / `0x01038CB2`) to build the
   client’s detail/confirmation UI and clear its waiting flag.
4. When the user confirms, `task_hall_accept_prompt_dispatch` (`0x010495E4`)
   loads `R1=1` then calls `SendTaskInfoReq` (`0x01047A7C`), which sends
   `WT 1/6/11 {taskinfo=<task id>}`. Only this request enters the server’s
   acceptance, start-item and persistence checks.

The trace stops after step 2. The existing server must not add a speculative
failure banner there; doing so would contradict the same offer result and
would not repair the missing client confirmation action.

## Rejection-feedback gap

The existing `6/11` handler preserves state correctly but has a visible reason
only for one known case: a full backpack while receiving a task’s start item.
Other precise rejections (level, unfinished prerequisite, already-active or
completed task, expired NPC offer, and a durable-state failure) all collapse to
`6/11 {result=1}` without a user-facing reason.

The client-native feedback contract is already known:

```text
6/11 {result=1} + [request-tail 25/5 {result=4}] + 25/11 {result=8, info=GBK}
```

`net_handle_info_banner_state` (`0x01010C7E`) displays `25/11.info` only for
`result=8`. The reason object must therefore follow the task result and the
request’s own `25/5` completion acknowledgement; it must not replace the
failed `6/11` or claim success.

## Implemented correction

`src/server/mock_server_scene_sync.c` now has a read-only diagnostic companion
to the existing task-definition predicate. It returns the first actual failed
precondition: level, current state (accepted / ready to submit / completed), or
unfinished prerequisite.

The existing `6/11` handler retains its acceptance transaction and its
`result=1` failure contract. On a real failure it now appends exactly one GBK
`25/11 {result=8, info=...}` after the original result object and optional
request-tail `25/5` acknowledgement. The known full-backpack reason remains
specific; state-read and durable-write failures get explicit operational text.
The bounded `mock_task_accept_rejected` log records task, role, reason code,
offer context, backpack condition and response-object count.

When a dynamic NPC is the only task source and the same predicate makes its
offer unavailable, its regular parser-backed dialogue field displays that
reason. It does not create a fake action. If another XSE task action is already
present, the dialogue remains owned by that valid action.

No new response object is added to the proven valid task-2 detail route.

## Verification

- Reproduce an actual confirm action and verify the presence of `6/11`.
- For valid task 2, assert `6/11 result=0` plus the active-task record.
- For every known rejection, assert `6/11 result=1` plus one `25/11 result=8`
  carrying GBK text, with no inserted task row or item grant.
- Keep the current valid detail path (`6/10 result=0`) banner-free.
