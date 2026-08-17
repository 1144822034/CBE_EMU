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

## Multi-Monster Dodge Identity Repair (2026-08-17)

### Trigger and First Divergence

In an encounter containing three copies of the same monster type, when the
first monster's counterattack was dodged, the two later counterattacks also
arrived as zero-damage `actioninfo` children with `child_flag=3`.  The client
was not making a shared dodge decision: `mmBattle:HandleBattleActionMsg`
consumes each child independently, and the established `0x2456` display path
maps every zero-damage counter child to `闪躲`.

The first incorrect state was on the mock-service side before `4/6` assembly.
`vm_net_mock_battle_enemy_damage_to_role` seeded its deterministic hit test
with only the monster type id.  Copies of one type therefore had equal
`session/turn/role/salt` inputs and necessarily produced the same hit result.
This affected item counters, strict and fallback `4/2` operate bundles, the
deferred enemy-turn response and failed-escape counters.

### Fix

The hit-roll salt now includes the attacking monster's original enemy wire
slot.  That slot is generated from the live encounter index and is stable for
the battle; it is intentionally kept separate from the optional
`CBE_BATTLE_COUNTER_ACTOR_WIRE_SLOT` display override.  Thus the same monster
in a repeated response keeps its result, while different copies of the same
type use independent deterministic inputs.

All counter paths pass that original wire slot to
`vm_net_mock_battle_enemy_damage_to_role`; the deferred paths also use it to
select the active enemy modifier.  The outgoing contract is unchanged: a
miss remains a zero-damage `actioninfo` child with flag `3`, and a hit keeps
the existing damage/flag `0` encoding.

### Regression Evidence

`scripts/battle-enemy-dodge-identity-regression.c` is a pure server fixture:
it neither opens a listener nor connects to MySQL.  It fixes a role, turn and
monster stats, then finds one deterministic session where the first live
three-monster wire (`2`) misses while the next (`3`) hits.  It also checks the
three salts are distinct and repeated evaluation of each wire is unchanged.

Commands run from the repository root:

```powershell
make -j2
gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 -ffunction-sections -fdata-sections -w scripts/battle-enemy-dodge-identity-regression.c obj/server/gifDecode.o obj/server/mystd.o obj/server/mysql-client.o obj/server/md5.o '-Wl,--gc-sections' -o tmp/battle-enemy-dodge-identity-regression.exe -lpthread -liconv -lm -lkernel32 -lws2_32
.\tmp\battle-enemy-dodge-identity-regression.exe
```

Observed result:

```text
battle-enemy-dodge-identity-v1 passed: session=2 turn=7 wires=2/3/0 hit=0/1/1 actioninfo=3/0/0
```

This fixture covers the deterministic settlement boundary and the known
actioninfo flag mapping.  The live client still needs only normal battle
input to consume the unchanged `4/6` packet; no client memory, registers or
CBE code are modified by the fix or its test.
