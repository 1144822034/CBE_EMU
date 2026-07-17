# 2026-06-26 Local Role DB

## Goal

Move mock-server player state from scattered constants/env defaults into a local
server-side role database.

Current contract:

- store a role list locally, max 5 roles for the title role-list parser
- allow 0 persisted roles after deleting the last role; the title client then
  displays only its create-role sentinel row
- persist HP, MP, money, scene, position, level, total EXP, and backpack items
- derive level from cumulative EXP thresholds: level 2 starts at 100 EXP,
  level 3 at 300 EXP, level 4 at 600 EXP, and each next level costs 100 more
  than the previous level
- grant battle rewards through the monster stat table; poison slime currently
  grants 5 EXP and 5 copper
- keep position and backpack state in the selected role row; do not use legacy
  mock-wide position or backpack state as a source of truth
- keep equipped item ids in the selected role row; do not use a global equipment
  state

## IDA Evidence

### Title Role List

`mmTitleMstarWqvga.cbm:0x3544` parses the title `actorinfo` field as a compact
role table:

```text
u8 count
repeat count:
  tagged u32 roleId
  tagged u8 job
  tagged u8 sex
  string name
  tagged i16 level
```

The parser clamps count to 5. It does not read HP, MP, money, scene, or position
from this title payload.

### Scene Actor Info

`Jianghu OL.CBE:0x0100FA88` parses the scene/login `actorinfo` player blob.
The mock keeps the recovered stream shape and now sources these fields from the
active local role:

```text
roleId, job/visual, sex/visual, name,
guildId, guildName/display,
level/status word,
hp, hpMax, mp, mpMax,
derived charm/stat words,
exp summary,
money/gap09C0,
backpackCapacity,
title/sect/spouse display strings,
target/grid fields,
actor resource,
scene key,
motion/grid words
```

Relevant parser write evidence:

- `0x0100FAC4` 把首个 `u32` 写入 actor `+100`，它是角色 ID；
  `0x0100FB22` 把角色名后的第二个 `u32` 写入 actor `+104`，它是帮派 ID，
  不能再次发送角色 ID。帮派菜单直接以该值是否为零区分未入帮/已入帮；
- `0x0100FBA8..0x0100FBE4` reads six tagged integer fields and stores them into
  the actor property word table around actor offsets `+288..+306`; the mock now
  fills these with derived strength/agility/wisdom/endurance/charm/reserve
  instead of zero.
- `0x0100FC84` copies a fixed 10-byte string into actor `+256`; this is the
  short display title slot, so the mock now sends the wealth title there instead
  of the role name.
- `0x0100FCEA..0x0100FD02` reads the post-resource i16 and mirrors it into the
  scene status level slot; the mock now sends the normalized role level instead
  of `0`.

### Scene Position

`Jianghu OL.CBE:0x010396D6` and `0x01039890` parse scene-enter/change results
with a scene string plus `posinfo`. Existing safe-position adjustment remains in
place; the final safe scene/x/y now writes only the active role record.

### Backpack

`Jianghu OL.CBE:0x01039952` parses `1/30/21` by reading `gridnum`, then raw
`iteminfo` rows of `itemId`, `seq`, `count`, and the common item-extra block.

`mmGameMstarWqvga.cbm:0x418C` parses `1/17/1` full-list `iteminfo` as a counted
row list of `itemId` plus the same common item-extra block. Both response paths
now source rows from the active role's backpack array.

### Battle Settlement

`mmBattleMstarWqvga.cbm:0x743C` handles battle settlement object `1/4/7` and
reads, in order:

```text
exp, lastexp, curexp, persentexp,
energy, energymax, gold, level, result, bagstatus, hp, mp,
itemnum, iteminfo, autorevive
```

Therefore EXP/level/gold/HP/MP must be sent in subtype 7, not guessed from a
later scene fallback.

## File Format

Primary file:

```text
nvram/jhol_mock_roles.bin
```

Header:

```text
magic      "JHR1"
version    3
activeRoleId
roleCount
roles[5]
```

Role row:

```text
u32 roleId
char name[32]
u8 job
u8 sex             // scene-side 0..1; title list sends sex+1
u8 backpackCapacity
u8 reserved
u32 level          // normalized from exp
u32 exp            // total EXP
u32 hp, hpMax
u32 mp, mpMax
u32 money
char scene[64]
u16 x, y
u8 backpackItemCount
u8 reserved
u16 nextBackpackSeq
equippedItemIds[8]
backpackItems[200]:
  u32 itemId
  u16 seq
  u16 reserved
  u32 count
```

Default role:

```text
roleId = 10001
name = GBK bytes for the existing role name
job = 1
sex = 0
hp/mp = 120/100
money = 1000
backpackCapacity = 20
scene = c00蓬莱仙岛_01.sce (Penglai TongQueTai)
position = 216,216
backpack = empty
nextBackpackSeq = 1
equippedItemIds[0] = 1001
```

Version 1 role DB files are upgraded in-place to version 4 by copying existing
role fields, keeping an empty backpack for each role, and assigning starter
equipment. Version 2 and 3 files migrate to version 4 by keeping the existing
backpack/equipment rows while normalizing the old default `40`-slot backpack to
the new `20`-slot baseline whenever that does not discard occupied rows. The
old `nvram/jhol_mock_player_pos.bin` mirror is no longer read or written.

## Server Behavior

Login and scene enter:

- `vm_net_mock_build_actor_info()` reads active role ID/name/job/sex/level,
  HP/MP, EXP, money, backpack capacity, scene, and position.
- role level is normalized from total EXP on load/save paths. The threshold for
  level `N` is `100 * (N - 1) * N / 2`; the EXP required from level `N` to
  `N + 1` is `N * 100`.
- scene/login actorinfo uses the active role name for its role name, display
  name so map-side role information matches title selection.
- scene/login actorinfo 的首个 ID 是 `roleId`，第二个 ID 是真实 `guildId`；未入帮
  发送 `guildId=0`，已入帮发送 `guild_members.guild_id`。客户端
  `HandleSceneMenuNavigation(0x0101AED2)` 与
  `HandleGuildMenuAction(0x0103D15A)` 都直接检查 actor `+104`，因此不能用角色 ID
  或“无帮派”文字代替该状态值。
- scene/login actorinfo now derives display-only RPG properties from the active
  role instead of writing zeros:
  - title is wealth based through the designation catalog documented in
    `2026-07-02-role-designation-page.md`; only titles whose money requirement
    is met are returned by the `23/1` title page list;
  - display/sect 查询 `guild_members -> guilds` 并显示真实帮派名；未入帮时显示 `无帮派`，不再把 `散人` 当作帮派名；
  - spouse defaults to `无` through the group/type-1 `name` field instead of the
    old hard-coded `Codex`;
  - level words default to the normalized role level, so a fresh role displays
    level `1` in the property panel;
  - strength/agility/wisdom/endurance/charm are derived from level/job and the
    role's equipped items. Equipment data is parsed from local `equip.dsh`.
- actor motion-resource generation uses the active role job/sex as its default,
  so the map sprite resource follows the selected role unless a `CBE_ACTOR_*`
  environment override is explicitly set.
- `vm_net_mock_append_login_success_object()` sends EXP bracket fields:
  `lastexp` is the current level start threshold, `curexp` is the next level
  start threshold, and `persentexp` is current-level percentage progress. The
  property page renders current EXP from `actor+0xB0 - lastexp` and the
  denominator from `curexp - lastexp`.
- `persentexp` is encoded as a normal u32/integer object field on login and
  battle settlement paths. The scene and battle parsers both read it through
  object getter `+0x44`; the later client-side `STRH` only narrows the UI cache.
  Runtime negative: sending the field as u16 left that progress cache at `0`
  because the client reads it through the integer getter before narrowing it.
- a runtime actorinfo probe that put EXP bracket values into the early
  HP-adjacent scalar slots changed the scene node from `battleHp=120/120` to
  `battleHp=120/100`, so those scalar slots stay HP/status-shaped until more
  parser evidence says otherwise.

Title role list:

- `vm_net_mock_build_title_role_list_actorinfo()` emits all stored roles up to
  the client limit of 5.
- title role-list compact actorinfo emits `jobIndex = role->job - 1` and
  `sex = role->sex + 1`. This matches `mmTitle:0x3544` and the create-success
  row layout at `mmTitle:0x5324`.
- role select handles request `1/1/6`, parses `actorID`, and updates
  `activeRoleId` before scene/login actorinfo is built. Scene helpers read
  `scene/x/y` from that active role row.
- title role create handles request `1/1/7`, appends a persisted role when
  capacity allows it, starts the new role at Penglai TongQueTai
  `c00蓬莱仙岛_01.sce @ (216,216)` with an empty backpack, and returns
  `actorid/result` to the title parser.
- role DB load repairs duplicate legacy rows that were previously persisted
  with the default name after a create-payload decode miss. The first default
  role keeps the GBK default name; later duplicate default rows become stable
  fallback names such as `Role10002`.
- title role delete handles request `1/1/8`, removes the matching persisted
  role by `actorID`, and returns `result=0` only on success.
- relational role saves preserve existing `account_roles` rows and update them
  in place. Only role IDs that actually disappeared are deleted. This is
  required because guild membership uses `ON DELETE CASCADE`; the former
  delete-and-reinsert save path dissolved a guild during ordinary role-select,
  position, or battle-state persistence.

Money sync:

- shop `coolmoney` reads the active role `wcoin` balance.
- group/type `money` and generic game-type `money` read the active role money.

Backpack:

- scene bootstrap `30/21` emits the active role's backpack rows and uses each
  row's persisted `seq/count`.
- backpack UI `17/1` emits the active role's backpack rows and capacity.
- NPC/shop buy `17/2` adds or stacks the purchased item into the active role
  before returning `14/3 { seq, result }`.
- item `806` (`背包扩容`) consumes one card per use, increases persisted
  capacity by `5`, and caps at `200`. The server follows the normal item-use
  success packet with a separate `17/1` refresh so the client re-reads the new
  capacity through its backpack parser.

Battle:

- battle start reads active role HP/MP/ID/name.
- battle settlement applies reward only when the enemy HP is zero, player HP is
  nonzero, and the current battle serial has not already been rewarded.
- battle settlement sends `lastexp` as the current level start threshold after
  the reward, `curexp` as the next level start threshold after the reward, and
  `persentexp` as current-level percentage progress after the reward.
- default monster reward is `10` EXP and `0` gold. `CBE_BATTLE_REWARD_EXP` and
  `CBE_BATTLE_REWARD_GOLD` remain focused experiment overrides.

## Validation

The normal `make` link to `bin/main.exe` was blocked because the executable was
in use. Since the Makefile does not track the included `src/mock-server.c`, the
current source was validated with:

```text
gcc -g -w -c src/main.c -o obj/main.codex.o
```

That temporary object compiled successfully. A same-command temporary link to
`bin/main.codex-build.exe` also succeeded. The temporary object and exe were
removed afterward.
