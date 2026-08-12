# Battle Secondary Stat Settlement (2026-08-12)

## Audit Result

The equipment catalog and role-stat builder already produce `hit`, `dodge`,
`crit`, and `resist`, including valid equipment, enhancement and active battle
modifier contributions.  Before this change no damage entry point read those
fields: normal attacks and offensive skills only used attack/defense, while
enemy attacks only used attack/defense.  The values could therefore appear in
the attribute UI without affecting any battle result.

## Client Boundary

`mmBattle:HandleBattleActionMsg(0x6EB0)` reads server-produced target slots and
HP deltas from each `actioninfo` child.  It does not provide a client-side
damage or chance calculation.  A zero HP delta is a valid no-damage action;
the battle action stays in the existing `1/4/6` contract.

## Server Rule

The original server probability table is not present in the client or local
resources, so these are deliberately documented mock-server rules rather than
claims about the original production formula:

- player hit compares role `hit` to level-derived monster evasion;
- monster hit compares level-derived monster accuracy to role `dodge`;
- player critical chance is a bounded rating conversion and increases the
  already-resolved outgoing damage by 50%;
- `SPIRIT` and `ELEMENTAL` monster damage is treated as magic and is reduced
  by role `resist` after defense; physical families retain the existing defense
  model.

The concrete current balance conversion is:

```text
role dodge       = 3 + level / 2 + agility / 2 + equipment dodge + modifiers
monster accuracy = 75 + monster level * 4
monster hit rate = monster accuracy / (monster accuracy + role dodge), clamped 40%..95%

monster evasion  = 20 + monster level * 2
player hit rate  = role hit / (role hit + monster evasion), clamped 60%..95%
player crit rate = role crit / (1200 + role crit), capped at 35%
critical damage  = resolved damage * 1.5, rounded up
```

The roll seed derives only from battle session, turn, active role and action
identity, so duplicate network polls cannot reroll an already selected action.
All chance values are bounded, preventing an equipment value from creating a
guaranteed hit, guaranteed dodge, or unbounded critical rate.

## Integration Rules

- player misses remain a normal actioninfo record with zero damage; they do not
  turn into a malformed empty response;
- zero damage is no longer coerced to one by the shared role-damage helper;
- every live monster counter now remains an `actioninfo` record even when a
  dodge produces zero damage; `HandleBattleActionMsg` needs that record to
  enqueue and finish the monster's action.  Only aggregate damage accounting
  omits the zero, so a dodged enemy cannot suppress its own or a later living
  enemy's turn;
- the main `mock_battle_operate` trace now records per-target `hit` and `crit`
  flags next to the authoritative damage values for manual validation.

## Effect Display

The native GBK labels `闪躲` and `暴击` are selected by
`mmBattleMstarWqvga.cbm:0x2456`: `actioninfo.child_flag=3` renders `闪躲`, and
`child_flag=2` renders `暴击`. The server now writes those verified values for
all ordinary/skill player attacks and for every monster counter path:

- miss: zero HP delta plus flag `3`;
- critical: resolved damage plus flag `2`;
- ordinary hit, healing, control, timed effects and DOT: existing flag `0`.

Physical attacks remain action type `0` with no effect tail. Skills and item
use retain their existing action type `1`/`2` `eidolon.dsh` effect-index path;
the child flag only adds the native outcome text and does not substitute a
spell/item effect.
