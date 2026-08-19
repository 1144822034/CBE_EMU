# Battle task-item drop classification

Date: 2026-08-19

## Reproduction and first divergence

Configure a monster drop whose item is a task item, enter battle with a role
that has not accepted a task requiring that item, and settle the battle.  The
expected result is no item grant and no backpack refresh row for that drop.

The existing pre-grant gate only classified an item as task material when its
ID appeared in an **enabled** runtime task requirement.  If an administrator
disabled or replaced that task, or configured another shipped task item that
was not referenced by the enabled catalog, the gate returned
`task_material=0`.  The battle reward path then treated it as an ordinary drop
and granted it without a corresponding accepted task.  This classification is
the first incorrect state; the later `4/7` settlement is only the visible
effect.

## Evidence

- `item.dsh` column 5 (`类别`) is the item semantic category used by the
  client and service catalogs.  Category `11` contains 66 shipped task-item
  IDs: `7..71` (with the resource's gaps) and `80`.
- `task.dsh` has 51 distinct type-1 requirement IDs, including one non-category
  item.  Therefore an enabled-task-requirement scan is not a complete task-item
  classifier; at least 15 category-11 items are outside the shipped requirement
  set before database overrides are considered.
- `mmBattleMstarWqvga.cbm:sub_743C` (`0x743C`) parses `4/7` fields including
  `result`, `bagstatus`, `itemnum`, and `iteminfo`.  It performs no task-state
  lookup, so eligibility must be decided before the service mutates the role
  backpack.
- The authoritative accepted-task relation is
  `account_role_tasks`.  State `1` means active; state `2` has already met its
  requirements and must not continue to produce collection materials.

## Contract and change

1. Classify an ordinary item as task material when `item.dsh` category is `11`.
2. Retain task-requirement classification as a compatibility path for custom
   tasks that deliberately collect an item from another category.
3. Grant a classified task material only when the active role has a matching,
   enabled task definition with persisted state `1` and positive remaining
   requirement.
4. Cap the grant to that remaining requirement.  A missing task-state query is
   fail-closed and cannot turn a task material into an ordinary drop.
5. Ordinary drops keep their configured probability and do not require a task.

The change remains in the existing battle pre-grant owner.  It does not alter
the `4/7` packet parser contract, client memory, battle state, or event order.

## Validation plan

- Deterministic unit regression with an in-memory item/task catalog and task
  state snapshot:
  - category-11 item with no accepted task: classified, remaining `0`;
  - category-11 item with active matching task: remaining requirement returned;
  - the same task in state `2`: remaining `0`;
  - ordinary item with no matching task: not classified;
  - ordinary item required by a custom active task: classified and eligible.
- Run `make -j2`.

## Validation result

- `scripts/run-battle-task-item-drop-policy-regression.ps1`: passed all six
  classification/state cases without opening a socket or changing a database.
- `make -j2`: the updated server translation unit compiled successfully.  The
  final link could not replace `bin/jh-online-server.exe` because the user's
  existing service process (PID 11348, port 19090) held that file open.
- The same freshly compiled object set linked successfully as
  `tmp/jh-online-server-task-item-drop-check.exe`, proving the code and server
  linkage while leaving the user's process untouched.
- A live client battle was not automated against the occupied service.  After
  the user restarts with the rebuilt binary, the observable settlement line is
  `mock_battle_drop_gate`: an unaccepted category-11 drop must show
  `task_material=1 remaining=0 eligible=0 grant=0`; an accepted active matching
  task must show positive `remaining` and may grant according to its configured
  rate.
