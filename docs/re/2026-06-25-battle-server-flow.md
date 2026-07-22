# 2026-06-25 Battle Server Flow

## phase

Restore the first monster battle loop:

- touch a monster and enter battle
- render the requested enemy instead of the player role
- make the normal attack button produce a physical attack
- include an enemy counter action while the enemy is alive
- send a battle settlement with visible experience fields

## request

Battle entry is driven by the existing scene interaction request:

```text
WT object: 1/4/1
stable fields: id, index, posx, posy
optional field: moveinfo
```

Battle operation is driven by:

```text
WT object: 1/4/2
stable fields: index, Operate
normal attack: Operate == 0
skill/spell: Operate == selected_skill_id + 2
```

IDA evidence for the normal attack request is `mmBattleMstarWqvga.cbm`
`Callback_Unknown2(0x2B50)`: when `a2 == 4 && a3 == 2`, the client writes
`index = sub_2B26(*(a1 + 5))`; if no selected skill record exists, it writes
`Operate = 0`. Therefore a visible skill animation after pressing normal attack
is caused by the server `actioninfo` response, not by the request.

For selected skills, the same request builder reads the selected battle-skill
record pointer at `r9+0x40B4` and sends `Operate = skillId + 2`. The server
must map `skillId` through `skill.dsh` column `技能图片` before writing the
action effect index. That value is an `eidolon.dsh` sequence and selects the
battle-effect actor copied by `HandleBattleActionMsg(0x6EB0)`. Sending the raw
skill id as the effect index is invalid:
runtime with `Operate=203` for skill id `201` tried to use effect `201` and
crashed during effect allocation. Sending column `法术标示` was parser-safe but
visually wrong: `201 绯炎幻法1` has `法术标示=15`, which points to
`f_sword2.actor`; its correct battle visual is `技能图片=7`, which maps through
`eidolon.dsh` to `f_flame1.actor`.

The same `skill.dsh` row also drives MP cost and damage. Column `耗费法力` is
the battle skill cost; for `201 绯炎幻法1` it is `5`. Column `生命变化` is a
signed base HP effect; offensive skills use its absolute negative value as the
minimum damage. Columns `力量系数`, `敏捷系数`, and `智慧系数` add scaled player
attributes before the normal soft defense curve. For example, `201 绯炎幻法1`
has `生命变化=-30` and `智慧系数=110`, so it must not reuse the normal-attack
damage path. Runtime and IDA evidence now separates three MP paths. The mock
server persists `role->mp -= 耗费法力`.
Subtype `4/6 actioninfo.valueB` for a type-1 child is the target MP change
paired with that child, not the caster skill cost and not the post-cost
absolute MP; for an offensive HP-only skill, keep this field `0`. The same
subtype-6 object must carry `teaminfo` for the caster: `InitActionSlot_B(0x6DBC)`
reads one triplet per team member and copies the third value into `unit+1344`.
`DrawBattleSceneBg(0x4BE8)` later assigns `attacker.currentMP = attacker+1344`
at the end of type-1 playback, so omitting `teaminfo` leaves that cache as `0`
and clears visible MP after a spell. Subtype `4/7` field `mp` remains the
settlement/recovery display delta; default battle-end MP recovery is `0`,
overrideable through `CBE_BATTLE_RECOVER_MP` only for explicit experiments.

Current contract: a skill response uses `skill.dsh` column `目标指向` to choose
the type-1 child list. A single-target enemy skill (`目标指向=3`) has one child;
an enemy-wide skill (`目标指向=4`) has one child for every live enemy; and a
positive-HP friendly-wide skill (`目标指向=2`, such as 三花聚顶) has one child for
every living party member. Enemy child `valueA` is a negative target HP delta;
friendly-heal child `valueA` is a positive, max-HP-clamped recovery amount.
For a timed friendly-wide stat effect (`目标指向=2`, `生命变化=0`, nonzero `时效`
and columns 16--24, such as 神臂担山), each living party member instead has a
zero `valueA/valueB` child under the single type-1 record.  The duration and
stat changes are server battle-state fields, not fake HP/MP deltas or durable
role attribute updates.
All use target MP delta `0` in `valueB`; the mapped battle-effect index from
`skill.dsh` column `技能图片` is record-wide. The same `4/6`
object carries `teaminfo = roleId, currentHp, postCostMp`, and the mock server
also updates the selected role's MP in the local role database. `actionnum`
still permits multiple records: if the enemy survives the skill hit, append the
normal monster counterattack record in the same subtype-6 response. If the skill
kills the enemy, append the terminal record and include subtype `4/7` in the
same terminal response so the result panel has populated reward caches.

Negative evidence: encoding the caster as a second child in the same type-1
record made both the monster and the player receive the spell visual, because
the type-1 effect metadata is record-wide, not child-local.

Negative evidence: using action type `2` for the separate MP-cost record enters
the battle-item branch in `HandleBattleActionMsg` around `0x70a8..0x70b0`.
That branch dereferences the selected battle item pointer at `r9+0x40B8`;
spell operation does not populate that pointer, so runtime crashed with
`r1=0`, `r2=0x322`, and a null read.

## response

Battle start response:

```text
WT object: 1/4/5 by default for scene monster challenge
old fallback: 1/4/10
fields:
  side: 1 by default when CBE_BATTLE_PLAYER_ON_RIGHT=1
  battleinfo: tagged stream
```

`battleinfo` subtype 5 grammar from `HandleBattleStartMsg(0x66CC)`:

```text
u8 left_count
u32 scene_monster_index
u32 scene_monster_posx
u32 scene_monster_posy
u8 right_count
repeat right_count:
  u32 id
  u32 hp
  u32 hp_max
  u32 mp
  u32 mp_max
```

For subtype 5 the left fighter is not read from the stream. The client uses
`scene_monster_index/posx/posy` to select a scene actor row at `*(R9+13476)`,
then copies the monster id, name, HP/MP, and sprite from that row. If the index
row does not match `posx/posy`, `0x66CC` scans up to 25 active rows looking for
kind `2` with matching coordinates. This is the correct path for "touch scene
monster -> battle", because the left-side monster stays tied to the scene node
that was touched.

2026-06-30 update: the first subtype-5 byte is now used as the wild encounter
monster count. `HandleBattleStartMsg(0x66CC)` copies the selected scene monster
row once per left-side unit and has explicit placement branches for counts
`1`, `2`, and `3`. The mock therefore rolls `1..3` for scene-monster battles,
writes that value as `left_count`, and keeps the right-side player count at
`1` for solo battles. Server-side battle state stores the same count and keeps independent HP
slots for each monster wire (`wire 1..N`). The aggregate `enemyhp` trace value
is only the sum of those slots for logging, status, and victory checks. Do not
damage a pooled `per_monster_hp * count` value: runtime showed that this desyncs
the client's independent unit records and can make a monster attack another
monster or make terminal death fall on the player. Settlement EXP/copper and
per-monster drop rolls still scale by the count. The durable backpack refresh
remains the post-battle `7/7 type=1` additive row, with `count_delta` equal to
the number of items dropped in that battle, not the role's total stack count.

2026-07-15 team-battle update: `mmBattleMstarWqvga.cbm`
`HandleServerBattleCmd(0x7BD0)` routes both subtype `5` and subtype `10` to
`HandleBattleStartMsg(0x66CC)`. There is no separate client command for a
party member's passive entry. For subtype `5`, every right-side row is resolved
through `sub_66A4(0x66A4)`, which compares the row id with `teamRow+36` in up
to four imported team rows. `BattleScene_CreateCharList(0x642A)` imports that
team table from the main CBE before parsing the start packet. Therefore the
server must send the normal `1/4/5` object to each same-scene member and every
right-side id must be the id already present in that particular observer's
team table.

The mock now freezes the same-scene team members when the leader's `4/1`
challenge is accepted. The leader response and each passive member response
use `right_count=2..3`, followed by one vitals row per frozen member. Because
test accounts can share persistent role id `10001`, rows are encoded with the
same observer-specific `0x6Axxxxxx` collision mapping used by the team roster;
the observer's own id remains unchanged. Passive starts are delivered through
the existing event-7 scene poll as an isolated two-object packet:

```text
1/2/2 { moveinfo }       # seed the selected scene monster row
1/4/5 { side, battleinfo }
```

The battle event is emitted before ordinary movement/social poll objects so
the client cannot continue parsing scene updates after `0x66CC` switches into
the battle screen. Members in another scene, offline members, and stale scene
transitions are excluded. No CBE globals or screen state are forced.

### 2026-07-15 shared team battle actions

Runtime after synchronized entry exposed a second boundary. Both clients had
the same two-role `4/5` start, but each account's first `4/2` operation reduced
its own copy of the monster HP (`60 -> 45` and independently `60 -> 40`). The
generated `4/6` was returned only to the requester, while the peer received
only main-CBE group HP/MP notices. A group `5/11` update does not replay the
battle-module action queue, so it cannot synchronize monster HP or animations.

The action wire mapping is derived from three client functions:

- `mmBattle:sub_6BF0(0x6BF0)` rearranges the subtype-5 action/display table by
  copying the right-side party first and the left-side monsters after it.
- `mmBattle:sub_2B26(0x2B26)` converts an internal fighter index into the
  outgoing `4/2 index` when `side=1`.
- `mmBattle:CalcTargetSideIndex(0x6CE8)` converts each server `4/6` actor and
  child target wire back to the internal fighter array.

For subtype 5 with `side=1`, `left_count=M` monsters and `right_count=P` party
members, let the reordered display index be `d` (`party 0..P-1`, then monsters
`P..P+M-1`). The actual server wire is not that display index directly:

```text
display_to_wire(d) = d >= M ? d - M : P + d
wire_to_display(w) = w < P  ? M + w : w - P
```

Runtime negative evidence caught the earlier direct-order assumption. With one
monster and two members, the client selected the monster and sent `4/2 index=1`.
The direct-order mock changed that to target wire 2; `CalcTargetSideIndex(2)`
then resolved to display member 0, visibly producing a teammate-on-teammate
attack. The correct permutation for `M=1, P=2` is:

```text
leader display 0 -> wire 2
member display 1 -> wire 0
monster display 2 -> wire 1
```

The shared battle builder now uses this permutation for player actors, selected
monster targets, monster counter actors, death records, and live-monster lookup.
The request `index` is retained whenever it maps to a live monster.

The service now keeps monster slots and the turn counter in the service-local
team battle instead of trusting the restored per-account copy. Before handling
each `1/4/2 { index, Operate }`, it loads that shared snapshot and selects the
actor wire from the requester's frozen party position. After building the
normal `1/4/6 { actionnum, actioninfo, optional teaminfo }`, it commits the
shared snapshot and queues the same action object for every other participant.
The existing event-7 poll delivers queued battle actions before scene movement
or social notices.

Action events use a bounded eight-entry ring with a per-participant delivered
mask, so a participant can receive an older teammate action without replaying
its own newer action. Skill `teaminfo` role ids are rewritten through the same
observer-specific role-id mapping used by the team roster. On victory, only
the `4/6` action object is shared; each observer builds its own `4/7` settlement
under that account's restored role state, preventing the leader's EXP, money,
or drop row from being copied into another account.

The solo action builder normally bundles a complete player-plus-monster round
into every offensive `4/6`. That rule cannot be reused unchanged by a party:
runtime showed `bundle=1` and monster counters after each member's operation.
The service-local battle now also owns a round serial and an acted-member mask.
The required mask is recomputed from frozen members whose shared HP is nonzero:

```text
on member action:
  reject if member is dead or its bit is already set
  resolve_monsters = (acted_mask | member_bit) == alive_mask
  build the normal 4/6 with monster actions enabled only when resolve_monsters
  for a non-final member, commit server state but retain its actioninfo by submit order
  return a zero-object WT acknowledgement (never an empty 4/6 action list)
  on the final member, concatenate every retained player action before its
    player-plus-monster actioninfo and publish one combined 4/6 to all members
  reset acted_mask and the retained action list only after publishing that round
```

The first implementation only suppressed monster records in non-final responses.
Runtime logs proved that its mask was correct (`resolve=0 bundle=0 counters=0` for
the first member and `resolve=1 bundle=1` for the last), but the first member still
received an independent `4/6`. Once that local list ended, the battle UI entered
its enemy phase before the peer had submitted. `HandleBattleActionMsg(0x6EB0)`
confirms that subtype 6 is the client action-list parser: it opens `actioninfo`,
reads `actionnum`, and creates the local action slots. Therefore the synchronization
boundary must cover the entire `4/6`, not merely the monster records inside it.

The revised path leaves solo combat unchanged. Team members who submit early get
a valid five-byte WT packet with zero objects, so their network request completes
without calling the battle action-list parser. Non-battle companion objects (for
example an item inventory refresh) remain in that direct acknowledgement. The
last still-alive member releases one combined player-actions-then-monster-actions
`4/6`; the same object is queued for peers. Duplicate or dead-member operations
also receive a zero-object WT acknowledgement and cannot advance shared enemy HP
or trigger the solo deferred-enemy-turn fallback.

2026-07-16 first retest negative evidence: the intended defer path logged
`team_battle_round_capture_failed`, immediately followed by `merge_failed` and
`team_battle_action_queue`. The fail-open branch therefore published the original
single-member `4/6`, explaining why runtime behavior had not changed. The cause
was an encoder/decoder mismatch inside the mock: `actioninfo` is emitted through
`vm_net_mock_put_object_raw()` as one object-entry `be16 value_len + value`, but
the capture code used the blob reader, which expects the extra nested length from
`vm_net_mock_put_object_blob()`. Team-round capture now parses only the bounded
response `1/4/6` object and walks its exact object-entry grammar. A successful
non-final submission must log `team_battle_round_capture` and
`team_battle_round_defer ... resp=5`; either `capture_failed` or an immediate
`team_battle_action_queue` remains a protocol failure.

2026-07-16 terminal-round correction: runtime with two monsters showed the
ordinary barrier working through the first kill. After round 1 left slots
`0/5/0`, the first submitter in round 2 killed the final monster while
`acted=00 alive=03 resolve=0`. The wrapper nevertheless released immediately
because it used `context.resolvesRound || enemyhp==0`. That terminal shortcut
bypassed the party mask even though no client had seen the killing action yet.

The team path now treats a non-final `enemyhp==0` as a pending terminal round:
it captures the killing `actioninfo`, suppresses both early `4/6` and early
`4/7`, and keeps `battleFinished` false. Remaining living members' `4/2`
requests count as submitted choices without fabricating attacks against an
already-dead target. The last required submission publishes the captured
attack/death list together with that observer's own `4/7`; peers continue to
build their own settlement when consuming the shared terminal event. Expected
trace order is `terminal_capture -> capture -> defer`, followed only after the
last member by `terminal_release -> action_queue`.

Member vitals are committed at the same shared-state boundary. The active
session's online HP, max HP, MP, and max MP are replaced with the shared battle
values before the next service poll, and an existing group subtype `5/11 hsp`
notice is queued for every roster observer. `HP=0` is preserved as a real
death value. This makes the death record inside `4/6` and the party HUD update
refer to the same snapshot instead of waiting until respawn changes the role
database again.

The scene actor HP/MP fields copied by subtype 5 are not populated by
`scene_node_find_or_create(0x0100EFC4)`. They live inside the raw move blob that
`scene_node_update_move_blob(0x01012A76)` copies to `ActorSceneNode+0x88`:

```text
node+0xB4 = moveBlob+44 = hp
node+0xB8 = moveBlob+48 = mp
node+0xBC = moveBlob+52 = hp_max
node+0xC0 = moveBlob+56 = mp_max
```

The server-side seed object is `1/2/2 { moveinfo }`, parsed by
`net_handle_actor_move_info(0x01012ADC)` case 2:

```text
moveinfo:
  i16 grid_pos_x
  i16 grid_pos_y
  u32 actor_id
  len16 raw_move_blob
```

Runtime correction: the bundled SCE has several 毒泥怪 rows with the same
`actor_id=105`. `scene_node_update_move_blob` matches by `actor_id` only and
therefore updates the first active 105 row, not necessarily the request
`index/posx/posy` row. The mock now chooses that same first-active row for both
the `1/2/2` moveinfo seed and the following subtype 5 battle start. This keeps
the battle start row, monster name/sprite, and HP/MP coherent until the unique
live monster instance id contract is recovered.

`battleinfo` subtype 10 grammar from `HandleBattleStartMsg(0x66CC)`:

```text
u8 own_count
repeat own_count:
  u32 id
  u32 hp
  u32 hp_max
  u32 mp
  u32 mp_max
  string name
  u8 visual_group
  u8 visual_variant
u8 enemy_count
repeat enemy_count:
  u32 id
  u32 hp
  u32 hp_max
  u32 mp
  u32 mp_max
```

Important placement correction: `HandleBattleStartMsg(0x66CC)` places the
first count at x=40 and the second count at x=200. Runtime visual evidence
showed the player sprite is on the right, so the default mock layout is:

```text
first record / left: scene monster selected by challenge index/posx/posy
second record / right: role id 10001, hp 120
```

The right-side record still goes through `sub_66A4(0x66A4)` template lookup.
There is not yet a safe packet contract for seeding that battle-only template.
The login-time `1/5/10` response must remain an empty roster while the role is
not actually in a team; using it as a solo battle-template cache creates a live
scene HUD row before any team exists.

2026-07-15 negative runtime evidence showed why a battle-time seed is wrong:
the `4/1` challenge response had `resp=248` and included a preceding `1/5/5`
player row.  The main CBE parsed that object through `net_handle_group_info`
before the battle transition completed, exposed it to
  `scene_draw_team_member_status_list`, and reached a null callback at
  `0x01014388`.  A later login-time solo-roster experiment failed at the same
  callback before scene entry. `CBE_BATTLE_PREFILL_PLAYER_TEMPLATE` and
`CBE_BATTLE_PREFILL_ENEMY_TEMPLATE` therefore default to `0`; they remain only
as explicit legacy experiment switches.

`sub_66A4(0x66A4)` matches ids against four local template rows at
`*(R9 + 13472)`, row stride `0x4C`, id at row offset `0x24`. If a scene
challenge request sends the player id `10001` or `0` as the monster id, the mock
normalizes it to `CBE_BATTLE_DEFAULT_ENEMY_ID`, default `105`.

Battle action response:

```text
WT object: 1/4/6
fields:
  teaminfo: tagged stream, optional but required for type-1 caster MP cache
  actionnum: u8
  actioninfo: tagged stream
```

`InitActionSlot_B(0x6DBC)` parses `teaminfo` before `actioninfo`. This is not
three independent tagged u32 values. The function calls the tagged-i32 reader
three times, but rewinds the cursor by two bytes after the first two calls:

```text
repeat current_team_count:
  u8  0
  u8  4
  u32 role_id
  u32 current_hp
  u32 current_mp
```

The three read starts are `row+0`, `row+4`, and `row+8`, so the returned values
are `role_id`, `current_hp` (ignored by current code), and `current_mp`.
For every active battle unit whose id matches `role_id`, the third returned
value is copied to `unit+1344`. Type-1 playback later restores the attacker's
current MP from that cache.

`HandleBattleActionMsg(0x6EB0)` grammar:

```text
repeat actionnum:
  u8 action_type
  u8 actor_wire_slot, remapped by CalcTargetSideIndex(0x6CE8)
  u8 child_count
  repeat child_count:
    u8 target_wire_slot, remapped by CalcTargetSideIndex(0x6CE8)
    u8 child_flag
    u32 value_a
    u32 value_b
if action_type == 1 or action_type == 2:
    u32 effect_index
    string effect_text
    u8 tail0
    u8 tail1
    u8 tail2
```

2026-07-01 correction: this `effect_index` is consumed as an index into the
battle effect sprite table loaded from `eidolon.dsh` (`序列号 -> 精灵名字`).
For item use, do not use `skill.dsh` column `法术标示` as the direct visual
index. Runtime negative evidence: `effect_index=16` plays
`f_thunder1.actor`; `eidolon.dsh` maps the HP-heal visual
`f_renew1.actor` to index `13`.

Important correction: for physical attacks, do not write the `effect_index`,
`effect_text`, and tail bytes. Those fields are only parsed for action type 1
or 2. Writing them after action type 0 shifts the next action record and can
make the second record look like a malformed or missing enemy turn.

With the legacy subtype 10 `playerOnRight=1` and `side=1` layout,
`CalcTargetSideIndex(0x6CE8)` uses the first/second counts copied by
`sub_6C5E(0x6C5E)`:

```text
wire 0 -> internal slot 1, the right-side player
wire 1 -> internal slot 0, the left-side monster

player attack: actor wire 0 -> target wire 1
enemy counter: actor wire 1 -> target wire 0
```

Subtype 5 also uses `CalcTargetSideIndex(0x6CE8)` through the battle-start
group counts. Static IDA evidence alone was misleading while reconstructing the
runtime-visible side order, so the implementation now treats the wire mapping as
runtime evidence first. Scene-monster battles with `side=1`, `left_count=N`, and
`right_count=1` currently use this server contract:

```text
wire 1    -> player actor in the normal attack/skill action
wire 0    -> single-monster target, and the last monster wire when N > 1
wire 2..N -> earlier monster wires when N > 1

player attack: actor wire 1 -> target wire request_index/fallback monster wire
enemy counter: first live monster wire -> target wire 1
terminal victory actor: the monster wire that lost the last HP slot
```

2026-06-30 negative evidence: after enabling `left_count=3`, sending
`actor=3,target=2` for a request with `index=2` made a monster attack another
monster. The server actor must stay on the player wire; do not derive it from
the requested target.

2026-06-30 correction: `Callback_Unknown2(0x2B50)` sends request `index` after
calling `sub_2B26(0x2B26)`, and `sub_2B26` is the inverse of
`CalcTargetSideIndex`. Therefore request `index` is already a wire slot, not an
internal monster slot. For `left_count=3`, selected monster internal slot `1`
is sent as request `index=2`. The response must use that request index directly
as the target wire; adding `+1` again can target the wrong unit.

2026-06-30 correction: multi-monster HP is per-slot. A player hit must subtract
from the selected live monster wire only, then recompute the aggregate total.
If the selected wire is already dead, redirect the server-side action to the
first live monster wire. Counterattacks also come from the first live monster
wire, not from a fixed fallback slot. This keeps `CalcTargetSideIndex`'s unit
mapping and the server's HP state aligned through the whole round.

2026-07-01 negative evidence: after trying `CBE_BATTLE_BUNDLE_ROUND=0` for
multi-monster fights, the current player request returned only the player's
action and armed `g_mockBattlePendingEnemyTurn`; the next player attack request
was then consumed by the pending monster action. Runtime looked like "player
acts once, next click monster acts first", and the last monster could stop
responding to normal attack clicks. Now that the subtype-5 player actor wire is
back to `1`, multi-monster fights also default to bundled round responses:
player action first, optional monster counteraction second.

2026-07-01 correction: a later single-monster runtime trace still emitted
`mock_battle_operate ... actor=0 target=1 ...`, and the visible result was the
monster acting first followed by the player-death prompt. That disproves the
previous subtype-5 default for the active client state. The mock's subtype-5
default is now restored to `player actor wire 1`, `monster target wire 0`, and
request `index=0` is accepted as the monster target instead of being forced to
wire 1. The terminal victory action must use the same monster target wire.

2026-07-01 multi-monster correction: scene-monster action target wires do not
follow a simple ascending formula once the left group has more than one monster.
Runtime evidence:

```text
two monsters: selecting the first can send index=2; target=0 still hit the second.
three monsters: target=2 hits the first, target=3 hits the second, target=0 hits the third.
```

The mock now uses explicit visual-order maps for subtype 5:

```text
N=1: slot0 -> wire0
N=2: slot0 -> wire2, slot1 -> wire0
N=3: slot0 -> wire2, slot1 -> wire3, slot2 -> wire0
```

`wire -> HP slot`, `HP slot -> wire`, and request-index translation must stay
on the same table.

2026-07-01 round-script correction: subtype `4/6 actioninfo` is consumed as a
round script by `HandleBattleActionMsg(0x6EB0)`. Runtime after fixing terminal
type-3 actions showed two separate requirements:

- Collecting monster actions only after applying player damage makes
  three-monster encounters display too few monster turns whenever the player
  kills one first.
- Letting the killed monster also counterattack prevents its visual row from
  disappearing.

The server now snapshots monster actors at the start of the round, then applies
player damage. If that hit kills a monster, final or non-final, the script emits
a type-3 death action for that monster and removes it from the counter list.
The remaining round-start monsters then counterattack in order. A three-monster
round where the player kills one target therefore returns one player action,
one death action, and two monster actions (`actions=4`, `deaths=1`,
`counters=2`). If no monster dies, the same three-monster round returns one
player action plus three monster actions (`actions=4`, `deaths=0`,
`counters=3`). A single-monster final kill returns one player action plus one
death action (`actions=2`, `deaths=1`, `counters=0`) before inline settlement.

2026-07-01 terminal-round ordering correction: runtime showed the final monster
HP reaching zero and `4/7` settlement being built, but the last player attack
animation was not visible. The trace order was `mock_battle_settle` before
`mock_battle_operate`, matching the old packet object order `4/7 status` then
`4/6 action`. The terminal round now keeps settlement inline but appends `4/6`
action first and `4/7` status second, so the client can consume the final
attack/death action before entering the settlement panel. The operate log marks
this case with `order=action6-first`.

2026-07-01 terminal-action negative evidence: after the order fix, final-kill
responses still contained an extra terminal record separate from the actual
death action. Runtime then showed the last remaining monster becoming
unattackable after an earlier monster died; later clicks returned tiny no-op
`4/2` responses. The server contract now treats `4/7` as the authoritative
battle-end signal and does not append the separate terminal action by default.
`4/6` still carries the actual visual actions for that round, including a
type-3 monster death action when the current hit drops a monster to zero HP.
The old separate terminal action can be re-enabled for experiments with
`CBE_BATTLE_TERMINAL_ACTION_ENABLED=1`; if used, it is skipped when a death
action is already present. Sending the role wire describes the player as the
fallen actor and can drive the client into the "player died" prompt.

Runtime evidence after the subtype 5 start fix:

```text
mock_challenge_battle_start id=105 requested=105 index=2 pos=(87,179) subtype=5 side=1 scene_start=1
battle_probe_unit[0] id=10001 type10=0 sprite=054045a8 name0=a3bdc0cf hp=120 hpMax=120
battle_probe_unit[1] id=105   type10=0 sprite=05412150 name0=e0c4beb6 hp=0   hpMax=0
battle_probe_action[0] type=0 actor=1 target=0 valueA=4294967286
battle_probe_action[0] type=3 actor=1
```

Runtime evidence after the moveinfo seed fix:

```text
mock_scene_monster_moveinfo actor=105 pos=(142,310) hp=20/20 mp=20/20 len=80
mock_challenge_battle_start id=105 requested=105 index=1 pos=(142,310) reqIndex=2 reqPos=(87,179) subtype=5 side=1
scene_actor_update_move actor=105 len=64 gridX=142 raw44=20 raw48=20 raw52=20 raw56=20
scene_probe_node[1] actorId=105 battlePos=(142,310) battleHp=20/20
battle_probe_unit[1] id=105 sprite=05412150 name0=e0c4beb6 hp=20 hpMax=20 mp=20 mpMax=20
```

Current expected runtime log after the subtype-5 wire and skill-damage
correction:

```text
mock_battle_operate index=<target-wire> operate=203 skill=1 action=1 actions=<round-script-records> effect=7 actor=1 target=<target-wire> damage=<skill-derived> enemyhp=<nonzero> rolehp=<after-counter> counters=<counter-monsters> deaths=<death-actions> deathActor=<dead-wire> counterdmg=<sum> mpcost=5 valueB=0 teaminfo=10001:<hp>/<postCostMp> mp=<before>/<after> response=4/6
battle_probe_action[0] active=1 type=1 actor=1 childCount=1 target=<target-wire> valueA=-<skill-derived> valueB=0 effect=7
battle_probe_action[1] active=1 type=0 actor=<monster-wire> childCount=1 target=1 valueA=-<monster-damage> valueB=0
```

If the skill or normal damage kills the last enemy, the default terminal packet
now keeps one player hit plus one monster death action in `4/6`, then appends
inline `4/7` settlement after the action object. No monster counterattack and
no separate terminal action should be present in the default path.

2026-07-01 escape contract: the client sends an empty `WT 1/4/4` object when the
player chooses escape. `mmBattle.HandleServerBattleCmd(0x7BD0)` dispatches
server subtype `4` at `0x7F06`, reads only the `"result"` field, and uses
`result=1` for the escape-success branch (`0x7F18..0x7F4A`) and `result!=1`
for the escape-failure branch (`0x7F4C..0x7F62`). The success branch clears the
battle/death flags itself, so the server returns only `4/4 { result=1 }` and
ends the mock battle session without reward settlement. The failure branch only
shows the failure notice; it does not play monster actions by itself. To model
the next monster turn, the failure response returns `4/4 { result=0 }` followed
by the usual `4/6 actioninfo` records for every currently alive monster. The
mock default success rate is 50%, with `CBE_BATTLE_ESCAPE_RATE` kept only as a
debug override.

2026-07-01 player-death correction: monster damage sent in `4/6 actioninfo`
must be clamped to the player's remaining HP. Server state already clamps
`g_mockBattleRoleHpCurrent` at zero, but sending a larger negative HP delta lets
the client animate HP below zero and can crash the battle renderer. Negative
runtime after adding `4/8 autorevive` inline showed the battle ending
immediately and a blank prompt, so `4/8` is not the normal prompt trigger. A
type-3 player death action is the trigger that opens the prompt, but order is
critical: it must be appended in the same `4/6 actioninfo` after every monster
damage/counter action that reduced HP to zero. Putting it before the final
monster hit makes the prompt appear too early; removing it or delaying it to the
next battle request leaves HP at zero with no prompt, because the client may not
send another battle request. Do not append `4/7` settlement on player death,
because `4/7` is the victory result panel/reward path. After the user clicks the
death prompt, the client sends `WT objs=1/7/14(result=2)` for the "no" branch;
the mock handles this as `builtin-battle-death-prompt-choice`, clears stale
battle session flags, applies the server-side death settlement, then sends
`1/20/1 { result=0 }` plus `1/30/1 { scene, posinfo }` for the respawn point.
Current server-side death settlement is:

- lose 10% of current-level EXP progress, clamped so death does not de-level;
- lose 5% of carried copper;
- revive with 30% max HP and 30% max MP;
- move the selected role to the same Penglai TongQueTai start point used by new
  characters.

IDA evidence:
`net_handle_simple_result_info` at `JianghuOL.CBE:0x1011434` handles
kind `20` subtype `1`, reads `result`, and clears the scene/network wait flag at
`0x101145C`; scene-channel subtype `30/1` is the normal scene-enter contract
consumed by `scene_handle_enter_with_scene_pos` around `0x010396D6`. Do not echo
the request-shaped `1/7/14(result=2)` packet back as a response: the main
business dispatcher runs `event_packet_init(..., 10, 19)` before response
dispatch, and the echo trips the generic unpack-error branch. Negative runtime
after returning an empty `WT` ACK (`len=5`, `objects=0`): no unpack error, but
the loading/progress bar stays active because no business handler clears the
wait flag. Battle start still applies a 1 HP floor to recover older local role
records that were already saved with zero HP before this settlement existed.

Older negative evidence collected while debugging battle skill use:

```text
battle_probe_unit[0] before action mp=100; after action mp=0 when valueB=0
mock_battle_operate ... mpcost=5 mp=100/95 response=4/6
battle_probe_action[0] ... childCount=1 ... valueB=95
battle_probe_unit[0] after animation mp=0, so enemy-child valueB is not the caster MP field
runtime after a two-child type-1 experiment showed MP first dropped by 5, then
fell to 0, while both player and monster played the spell effect; therefore MP
cost must not be encoded as another child of the same type-1 record
runtime with separate `costAction=2` crashed in the item-use branch because
the selected battle item pointer was null; use `costAction=0` instead
runtime with `costAction=0,valueA=0,valueB=-5` deducted MP first, then later
showed a 0 HP heal/change line.
runtime with `costAction=0,valueA=currentHP,valueB=postCostMP` removed the 0 HP
line but displayed `currentHP` as a heal. Therefore separate MP-cost actions are
disabled by default; carry MP cost only in the server-side role state.
runtime with a single non-terminal type-1 action and `valueB=-5` drove player MP
from 55 to 0 even though no subtype `4/7` settlement was sent. This proves
`4/6 actioninfo.valueB` is not a signed MP delta.
runtime with `actor=0,target=1,valueB=35` made the skill hit the player and
raised visible MP from 35 to 65, so offensive skill `valueB` cannot be the
post-cost absolute MP.
runtime with `actor=1,target=0,valueB=5` wrote `unit1Mp` from 20 to 25 after the
action response. Since subtype-5 remaps this child to the monster target, this
confirms `valueB` is target MP delta, not caster MP cost. The accompanying
`pc=05186d64 off=12d64` maps to soft-float `Float32_Add(0x12CB0)`, not
`InitActionSlot_A(0x6D1E)`; the earlier iteminfo hypothesis was a bad base
mapping. Follow-up IDA evidence in `InitActionSlot_B(0x6DBC)` showed that
subtype-6 `teaminfo` is exactly the missing caster MP cache: when omitted,
`DrawBattleSceneBg(0x4BE8)` restores the attacker's MP from zeroed `unit+1344`.
Negative runtime: sending `teaminfo` as three normal tagged u32 values made the
third read return `hp_low16 + next tagged header`, observed as `0x210004` when
HP was `33`, then crashed in the renderer. Encode one overlapped row instead:
`00 04, roleId32, hp32, mp32`.
runtime after deferring terminal `4/7` showed the result panel with all reward
fields as zero. By the time the next request asks for pending settlement, the UI
has already copied zeroed caches. Therefore terminal responses keep `4/7`
inline by default, while `hp/mp` remain explicit recovery deltas and default to
zero.
```

Implementation note: type-1 `effect_text` should carry the display magnitude
for the HP delta (`18` for `valueA=0xffffffee`), not the unsigned two's
complement value.

Battle settlement response:

```text
WT object: 1/4/7
fields:
  exp
  lastexp
  curexp
  persentexp
  energy
  energymax
  gold
  level
  result
  bagstatus
  hp
  mp
  itemnum
  iteminfo
```

`HandleBattleSettleMsg(0x743C)` reads `exp` first, then `lastexp`, `curexp`,
`persentexp`, and the remaining status fields. Bytes at `0x771C` confirm the
first key is the string `exp`.

Timing correction: `HandleBattleSettleMsg(0x743C)` clears and fills the battle
temporary display/state area at `r9+15724`. It snapshots the old actor EXP,
gold, and level fields, writes response `exp/gold/level` as the new totals, then
stores the visible result-panel deltas as `new - old`. Therefore `4/7.exp` and
`4/7.gold` must be post-reward totals, not per-kill deltas. The terminal action
opens the panel immediately, so `4/7` must be present in the same terminal
response by default. `hp/mp` remain recovery deltas and default to `0/0`.
Server-side role state still applies RPG level-up recovery: when awarded EXP
crosses a level threshold, the role's derived max HP/MP are recalculated and
current HP/MP are filled to those new maxima. This is persisted as role state;
it is not encoded as ordinary `4/7.hp/mp` recovery display unless a later client
contract proves a distinct level-up recovery field.

Dropped-item display: `itemnum` is separate from raw `iteminfo`. The recovered
row prefix is `ownerRoleId, displayFlag, itemId, itemName, rewardType, i16
value, u32 value`. Rows whose owner role id does not match the current player
are skipped by the client. Runtime negative evidence: `rewardType=1` enters an
equipment/detail registration helper and crashes with the short ordinary
consumable row; `rewardType=2` parses without crashing but reserves a blank row
in the current client. The visible ordinary drop line is therefore sent through
the same `4/7.fdata` settlement text field parsed at `0x7B08` and rendered by
the result panel at `0x4462`, while the durable item grant is persisted in the
role backpack.

## implementation target

Minimum server fix:

- map invalid battle enemy ids `0` and `10001` to `CBE_BATTLE_DEFAULT_ENEMY_ID`
  with default `105`
- default `CBE_BATTLE_PLAYER_ON_RIGHT=1`
- for scene challenge entry, default `CBE_BATTLE_SCENE_MONSTER_START=1`, send
  battle start object `1/4/5`, and let the left monster come from the touched
  scene actor row selected by `index/posx/posy`
- before the scene-monster battle start, default
  `CBE_BATTLE_SCENE_MONSTER_MOVEINFO=1` and send `1/2/2 moveinfo` for the same
  scene row so subtype 5 copies nonzero HP/MP from `node+0xB4..0xC0`
- keep subtype `1/4/10` only as a fallback/experiment path for non-scene starts
- keep login `1/5/10` empty when the role has no team and do not inject `5/5`
  into the battle-start response by default; recover a dedicated battle-side
  player-template contract before enabling the right-side lookup path
- default `CBE_BATTLE_ACTION_TYPE` to `0`, keeping type 1 and 2 available by env
  override for later skill experiments
- encode action type 0 records without effect tail fields
- for subtype 5 scene-monster starts under `side=1`, default battle wire slots
  to player `1`, enemy `0`; keep the old subtype 10 layout as fallback
- choose terminal action actor by HP-zero side: enemy slot for victory, player
  slot for defeat
- send subtype 7 settlement with `exp`, `lastexp`, `curexp`, and visible reward
  defaults; `lastexp` is the current level start threshold after reward,
  `curexp` is the next level start threshold after reward, and `persentexp` is
  current-level percentage progress after reward. Encode all three EXP fields
  as normal integer object fields: `HandleBattleSettleMsg(0x743C)` reads them
  through object getter `+0x44` and only narrows `persentexp` after parsing.
- include subtype 7 in the terminal action response by default; keep recovery
  `hp/mp` as explicit display deltas and default them to `0/0`.
- when reward EXP or an EXP item raises the role level, refill persisted current
  HP/MP to the newly derived maxima without treating it as ordinary battle
  recovery panel text.
- include `itemnum/iteminfo` for displayed ordinary item drops after the reward
  roll inserts the item into the role backpack.
- use negative two's-complement HP deltas in subtype `4/6`; positive values heal
- after each successful battle operation/item/escape response, publish the
  authoritative battle HP/MP counters to the active role before the service's
  presence capture runs; this drives the already recovered group subtype
  `5/11 { hsp }` path on every real change rather than only after settlement

## unknowns

- The exact semantic name of `child_flag` is still unknown. Current code keeps
  it configurable and defaults it to zero for both directions.
- Subtype 5 currently copies the touched scene monster art/name correctly, but
  duplicated local SCE actor ids mean the moveinfo seed can only target the
  first active row with that actor id. Recovering the live server's unique
  monster instance id remains the next precision step.
- The current `key:f` autotest trigger for touching a monster is not stable
  enough to validate every battle operation run. The server-side start contract
  is verified by `battle_probe`, but the next work item should add a reliable
  battle-entry automation path before tuning settlement UI.

## validation

Build:

```text
make
result: passed
```

Runtime smoke:

```text
make
result: passed

bin/main.exe --autotest --shot-ms=3000 --max-ms=100000 --actions=5000:key:f,17000:key:f,19000:key:q,23000:key:f,29000:key:f,35000:key:f,42000:tap:45:216,44000:key:f,50000:tap:63:247,52000:key:f,58000:key:f,64000:tap:48:214,66000:key:f,72000:key:f,78000:key:f,84000:key:f,90000:key:f
env: CBE_SCENE_POS_X=225, CBE_SCENE_POS_Y=116
env: CBE_BATTLE_SCENE_MONSTER_START=1
result: touched actor id 105, entered battle without crash, monster row uses name0=e0c4beb6 and sprite=05412150, no player-death prompt after terminal action
```

The key-only script is still unreliable for producing `4/1`/`4/2`; mixed
tap+key scripts are more repeatable until a dedicated battle-entry automation
helper exists.

Team-vitals smoke (`tmp/team-hsp-latency-validation/service.log`, 2026-07-15):
two online sessions formed a two-member team, client A sent a skill `4/2`, MP
changed from `100` to `90`, and the next normal scene polls delivered one
`5/11` to A and one to B with `hp=99/120 mp=90/100`. The local update used
wire id `10001`; the colliding remote role used that observer's `0x6Axxxxxx`
wire id.
