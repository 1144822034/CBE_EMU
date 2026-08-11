# 2026-06-28 Battle Item Use

## Runtime Trigger

Using a backpack item from the battle UI produced an unhandled request:

```text
WT 4/3 len=30 objects=1 first=1/4/3:21
```

This is separate from normal scene/backpack item use (`7/1`). The battle module
has its own request path.

## IDA Evidence

`mmBattleMstarWqvga.cbm:Callback_Unknown2(0x2B50)` constructs battle requests:

- subtype `4/2`: writes `index` and `Operate`
- subtype `4/3`: writes `index` and `seq`
- subtype `4/11`: writes `type`

The subtype `4/3` `seq` field is read from the selected item row at offset
`+276`, matching the role backpack row sequence used by the mock DB.

`mmBattleMstarWqvga.cbm:HandleServerBattleCmd(0x7BD0)` does not dispatch a
server response subtype `4/3`. Server-side action playback is subtype `4/6`,
which calls `HandleBattleActionMsg(0x6EB0)`.

`HandleBattleActionMsg(0x6EB0)` parses `actioninfo` records. Action type `2`
uses the action effect fields and then decrements the currently selected battle
item row (`+242`) when the action actor resolves to the player side. This
identifies action type `2` as the native battle item-use action.

Negative evidence: `4/4` is the escape result branch. The strings at
`0x8044/0x8050` decode as escape success/failure, so `4/4` must not be used as
the item-use acknowledgement.

## Mock Contract

Request detector:

```text
WT object 1/4/3
fields:
  index
  seq
```

Response:

```text
WT object 1/4/6
fields:
  actionnum
  actioninfo
```

The first action record is:

```text
action_type = 2
actor = player wire slot
target = player wire slot
value_a = HP actually restored
value_b = MP actually restored
effect_index = CBE_BATTLE_ITEM_EFFECT_INDEX or item-derived default
```

The mock consumes one active-role backpack row by `seq`, applies the item effect
from `item.dsh`, updates/persists role HP/MP/EXP, and returns the action packet.
By default a live enemy counterattack is bundled after the item action; set
`CBE_BATTLE_ITEM_USE_COUNTER=0` for focused rollback experiments.

Because battle rewards already proved that kind-`7` refresh objects can be
mixed into the same WT response, a successful battle medicine use appends the
count-sync object after `4/6`:

```text
1/7/11 { info=<row_count,seq,new_count> }
```

Runtime correction: reusing the scene item-use `7/7 type=2` path inside battle
was too aggressive. Battle action type `2` already decrements the selected
battle row in `HandleBattleActionMsg(0x6EB0)`. Appending `7/7 type=2` on top of
that made a visible `x2` medicine jump straight to `x0`. `7/11` is the narrow
main-dispatcher sync that updates the item row count without triggering the
second consume path.

2026-07-07 inference-based correction: IDA shows the decrement guard is keyed by
the action actor's side, not the child target side. For HP medicines, the mock
now defaults to a type-`1` self-heal playback plus `7/11` count sync instead of
type `2`. This keeps the heal visual/effect path but avoids the battle module's
hard-coded local item-row decrement branch. Runtime still needs confirmation for
the final visible count behavior across all battle UIs.

Multi-monster correction: battle item use must still follow the same round
script expectations as normal battle operate. When `CBE_BATTLE_BUNDLE_ROUND=1`,
the server snapshots all currently alive enemy wires and appends one
counterattack record per living monster after the item-use record. A three-
monster round therefore returns one item-use record plus three monster actions
unless the player's HP reaches zero earlier in the script.

If the selected `seq` cannot be resolved or the item has no usable effect, the
mock returns a narrow battle no-op `4/6` object with `actionnum=0` and empty
`actioninfo`, and does not consume DB state. Do not use action type `4` as this
fallback: runtime showed that it can leave the normal item-use flow and drop
back into the scene follow-up path.

## Item Visual Effect

`HandleBattleActionMsg(0x6EB0)` reads an extra u32 for action type `1` and
action type `2` after the target/value children. That value is stored in the
action slot at `+92` and then used to index the battle effect resource table
before the animation is played.

HP battle medicines should not use the old default `0` effect, and they should
not use `skill.dsh` column `法术标示` directly. Runtime negative evidence showed
that `effect_index = 16` plays a thunder visual. The reason is that the battle
action field indexes the `eidolon.dsh` battle-effect sprite table:

```text
eidolon.dsh columns: 序列号, 精灵名字
0  -> f_blood1.actor
...
13 -> f_renew1.actor
16 -> f_thunder1.actor
```

`JHOnlineData/f_renew1.actor` references `回复.gif`, matching the healing visual.
Local item data then identifies which items should use that effect:

- `item.dsh` HP medicines such as `301 小回春散` through `305 中长命散` and
  mixed HP/MP medicines `341..343 五龙膏` are single-target items with positive
  `生命变化`.

Therefore the mock now writes `effect_index = eidolon.dsh["f_renew1.actor"]`
for consumed battle items whose `item.dsh` effect restores either HP or MP.
In the current resources this is `13`; it covers HP medicines `301..305`, MP
medicines `321..325`, mixed medicines `341..343`, and the two reservoir flasks
`802/803`. `CBE_BATTLE_ITEM_EFFECT_INDEX` remains an explicit debug override,
including setting it to `0`.

## Runtime Source

Handler source name:

```text
builtin-battle-item-use
```

Trace line:

```text
mock_battle_item_use index=... seq=... item=... remaining=... effect=13 action=1 counters=3 armed=1 bundle=1 enemies=3 slots=20/20/20 sync=1 noop=0 response=4/6+7/11-actionType1
```
