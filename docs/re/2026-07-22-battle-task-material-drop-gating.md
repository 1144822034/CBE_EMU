# Battle task-material drop eligibility

Date: 2026-07-22

## Evidence and ownership

- A terminal battle action is settled through `WT4/7`.
- In `mmBattleMstarWqvga.cbm`, `HandleBattleSettleMsg` (`0x743c`) reads the
  settlement fields `itemnum` and `iteminfo` with the other result fields.  It
  has no task-state lookup or material eligibility branch.
- The server's durable mutation point is
  `vm_net_mock_battle_grant_reward_once`: it adds `stats.dropItemId` to the
  role backpack, then invokes `vm_net_mock_task_progress_after_battle`.

Therefore task-material eligibility is a server-side pre-grant rule.  It must
not be implemented by altering the client, suppressing a later packet, or by
performing cleanup after the backpack has already been changed.

## Contract

For a configured monster drop whose item ID is present in an enabled task
catalog material requirement (`requirementType1/2 == 1`):

1. The item may drop only while the active role has a matching
   `account_role_tasks.task_state = 1` row.
2. Its quantity is capped by the remaining requirement.  State `2` (ready to
   turn in) is ineligible.
3. The normal task-progress writer runs only after the successful backpack
   mutation, using that exact granted quantity.

Configured drops that are not task-material IDs retain their ordinary battle
drop behavior; they do not require an active task.  The battle log emits one
`mock_battle_drop_gate` line with the classification, remaining count, roll,
and granted count for diagnosis.

## Validation

- `make -j2` completed and the restarted local mock service is listening on
  `127.0.0.1:19090`.
- An isolated protocol regression used monster `1` (`item 27`, 100% test
  rate) and a temporary one-item task.  With no active task it observed
  `remaining=0 eligible=0 grant=0`, no `7/7` backpack delta, and no item
  mutation.  After inserting an active matching task it observed
  `remaining=1 eligible=1 grant=1`, a `7/7` backpack delta, and task progress
  `1/1` transitioning to state `2`.
- The regression restores its test role's backpack, task rows, role counters,
  and equipment-durability rows; the temporary task row is deleted afterwards.
