# Test-map Monkey Scene-battle Task

## Root Cause

The normal `server_tasks` type-2 objective carried only a monster id.  It
could therefore advance after that id was defeated in any scene.  A native
scene battle is more specific: the client copies a type-2 node from one loaded
SCE2 resource, so the task objective must also name that exact scene resource.

The reserved task `900001` is not a combat-task implementation.  Its
synthetic record uses collect type `1` and id `65535`, so battle settlement
cannot advance it.  Its legacy dialog-only state change is not battle proof.

## Contract

`server_task_scene_battle_targets` stores optional scene scope:

```text
task_id, requirement_slot (1 or 2), scene
```

A mapping is valid only when its slot is a type-2 objective, its monster id is
within the native scene-monster range `1..65535`, and its SCE key names an
existing server resource.  The catalog loads target rows after task rows and
does not issue nested MySQL queries from a result callback.

The task editor now has one optional SCE field per objective.  Save requires
an enabled scene-battle-monster draft with the same scene and monster id.  The
task is not offered or accepted until the existing deployment/capacity check
proves that target is ready.

Battle progress uses the real current scene and battle enemy already produced
by the normal client flow:

```text
type == 2 && enemy_id == requirement_id && current_scene == target_scene
```

Unmapped type-2 objectives retain their old id-only behavior.  No CBE memory,
register, PC/LR, instruction, request, or response byte is changed.

## Test-map Setup

Do not reuse task `900001`, and do not seed a user database automatically.
Use the normal administration pages:

1. Select the test-map SCE in scene-battle-monster administration.  Configure
   and explicitly deploy an enabled little-monkey kind-3 record; record its
   monster id as `E`.
2. Create an ordinary task with a new id.  Set one objective to type `2`, id
   `E`, count `1`, and set that slot's target SCE to the same test-map key.
3. Bind the task to the desired dynamic task NPC.  The existing binding can
   make that NPC both giver and receiver.
4. Complete the normal content-update/restart cycle, accept the task, collide
   with the real kind-3 monkey, win, and submit at the configured receiver.

The required runtime evidence is client-originated `WT4/1`, server
`WT2/2 + WT4/5`, then `mock_task_battle_progress` with the exact configured
scene and `state=2`.  Resource installation still has to precede this chain;
a missing Actor/SCE node or an index-zero request remains a separate earlier
resource/lifecycle fault.

## Validation

- `make -j2`: passed.
- `scripts/run-scene-battle-task-objective-regression.ps1`: passed.  The pure
  in-memory test covers exact scene/id match, wrong/unresolved scene rejection,
  wrong id rejection, a second mapped slot, malformed mapping rejection, and
  unmapped legacy compatibility.  It starts no client/listener and opens no
  database connection.
- Rebuilt and ran `obj/server/scene-battle-monster-field18-regression.exe`:
  passed.
- Rebuilt and ran `obj/server/scene-transition-entry-contract-regression.exe`:
  passed.  Its resource-only fixture emits expected no-MySQL diagnostics but
  does not connect to or mutate a database.

Final acceptance is one configured client run: task offer, `WT4/1 -> WT4/5`,
battle victory, task-state update, and normal NPC submission.
