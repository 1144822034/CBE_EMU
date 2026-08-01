# 2026-06-28 Player Attribute Model

## Goal

Build one server-side player stat model for Jianghu OL mock-server gameplay:

- role level supplies base attributes;
- equipped items add bonuses from local `equip.dsh`;
- HP/MP maxima, normal attack, and defense all come from the same model;
- battle damage uses a soft defense curve instead of raw subtraction;
- persisted role data keeps per-role equipment slots for later equip/unequip work.

## Client Boundary

No new client field was introduced in this pass.

- `JianghuOL.CBE:parse_actorinfo_response(0x0100FA88)` still consumes the
  existing scene/login `actorinfo` blob. The mock now fills the known HP/MP and
  attribute scalar slots from the unified model.
- The six property words seed `actor+0x122..` halfwords.  Word[4] maps to
  `+0x132` (**护甲** bare).  Panel **物攻** is `actor+0x130`, filled only by
  weapon wear-apply from `item+0xFA` — not by actorinfo.  Charm stays on
  `EXTRA132`.  See `2026-07-29-actorinfo-attack-word-mapping.md`.
- HP/MP keep bare `primaryBaseMax` / `secondaryBaseMax` (client re-add path).
- Do not push a full-blob `1/1/14` on login equipment bootstrap; refresh mode
  skips wire reads and desyncs.
- `mmBattleMstarWqvga.cbm:HandleBattleStartMsg(0x66CC)` still consumes battle
  start HP/MP records. The mock now seeds those records from the same model.
- `mmBattleMstarWqvga.cbm:HandleBattleActionMsg(0x6EB0)` still consumes action
  damage deltas. The mock now derives the delta from player attack and monster
  defense, or monster attack and player defense.
- `mmBattleMstarWqvga.cbm:HandleBattleSettleMsg(0x743C)` remains the persistence
  point for HP/MP/EXP/money.

## Client Authority (2026-07-29)

Follow `江湖OL.CBE` evidence; do **not** invent “主属性→攻 / 力量→血 /
敏捷→甲” unless a client path writes those halfwords that way.

### Login / actorinfo (server absolute)

On each login the server seeds absolute 力量/敏捷/智慧/耐力/气血/法力.
Observed job tables (attrIndex 0..4 = 力/敏/智/耐/魅) still match live
`bareStr/bareAgi/bareWis` for known roles — keep
`vm_net_mock_role_derived_attr`.

### Mid-session level-up (`0x01017FB6`)

When level rises **in an already-running session**, the client adds flat
per-level deltas from the actor job byte (0/1/2), then re-runs wear-apply:

| job byte | 气血 | 法力 | +0x84 | 力量(+0x122) | 敏捷(+0x124) |
| --- | --- | --- | --- | --- | --- |
| 0 | +25 | +15 | +12 | +4 | +6 |
| 1 | +20 | +20 | +6 | +10 | +3 |
| 2 | +15 | +25 | +3 | +6 | +12 |

It does **not** add 智慧、物攻(`+0x130`)、护甲(`+0x132`) on level-up.
Those stay actorinfo / equipment-apply owned. Login absolute formulas are
therefore not required to equal `(L-1) *` these mid-session rates.

### Property panel

UI labels include 智慧, but paint-8 halfwords (login role34 evidence) pair as
力量,敏捷,物攻,护甲,闪躲,命中,暴击,抗性.

| 行 | halfword | 客户端来源 |
| --- | --- | --- |
| 力量/敏捷 | +0x122/+0x124 | actorinfo word[0]/[1] + apply |
| **物攻** | **+0x130** | **仅武器** `item+0xFA` |
| **护甲** | **+0x132** | word[4] bare + 防具 `item+0xF8` |
| 闪躲 | +0x128 | enhance wire→4 |
| **命中** | **+0x12a** | **word[2]=hit**（不是智慧） |
| 暴击 | +0x12e | enhance wire→6 |
| **抗性** | **+0x12c** | **word[5] bare（0）+ wear `fec6` 装备抗性** |
| 智慧 | +0x126? | word[3] provisional / paint unresolved |
| 活力 | unresolved | — |

无武器时物攻为 0 是客户端契约。详见
`2026-07-29-actorinfo-attack-word-mapping.md`。

### Battle (mock server)

`playerStats.attack` / `defense` / soft mitigation remain **server-side**
damage authority. They are not the property-panel 物攻/护甲 halfwords.
See `2026-07-29-actorinfo-attack-word-mapping.md`.

## Role DB

The role DB is upgraded from version 2 to version 3.

Version 3 keeps all previous per-role fields and adds:

```text
equippedItemIds[8]
```

Slot mapping:

```text
0 weapon  (equip.dsh category 7 sword, 8 dagger, 9 staff)
1 helmet  (category 0)
2 chest   (category 1)
3 cloak   (category 2)
4 belt    (category 3)
5 legs    (category 4)
6 boots   (category 5)
7 ring    (category 6)
```

Version 1 and version 2 files migrate in-place to version 3. Migrated and newly
created roles receive one starter weapon based on job:

```text
job 1: item 1001 木制宽剑
job 2: item 1501 木制匕首
job 3: item 2001 木制杖
```

The starter weapon is stored as equipped gear, not as a backpack stack. The
backpack remains the existing per-role 40-slot inventory.

## Equipment Data

`bin/JHOnlineData/equip.dsh` is parsed lazily. Relevant columns:

```text
0  ID
3  等级
7  类别
8  护甲
9  攻击
10 生命变化
11 法力变化
12 力量变化
13 敏捷变化
14 智力变化
15 爆击变化
16 命中变化
17 躲闪变化
18 抗性变化
```

Equipment bonuses only apply when:

- the equipped item id exists in `equip.dsh`;
- the item category matches its stored slot;
- role level is at least the equipment level requirement.

## Base Attributes

The existing job tables remain the base model:

```text
job 1: strength 12 +3/L, agility  8 +2/L, wisdom  7 +1/L, endurance 11 +3/L, charm 3 +1/L
job 2: strength  9 +2/L, agility 14 +3/L, wisdom  8 +2/L, endurance  8 +2/L, charm 4 +1/L
job 3: strength  7 +1/L, agility  9 +2/L, wisdom 15 +4/L, endurance  7 +2/L, charm 5 +1/L
```

The increment uses `(level - 1)`, so level 1 is exactly the base row.

Visible attributes:

```text
strength = baseStrength + equipment.strength
agility  = baseAgility  + equipment.agility
wisdom   = baseWisdom   + equipment.wisdom
endurance = baseEndurance
charm = baseCharm + money / 100000
```

`equip.dsh` does not currently expose an endurance column, so endurance remains
level/job based until a recovered item field proves otherwise.

## Derived Combat Stats (server battle model)

Panel 物攻/护甲 follow the client table above. Mock **battle** still derives:

```text
maxHp   = 90 + level * 8 + endurance * 2 + equipment.hp
maxMp   = 70 + level * 9 + wisdom * 3 + equipment.mp
attack  = 6 + level * 2 + jobPrimary / 2 + equipment.attack / 3
defense = 4 + level + endurance / 2 + equipment.armor / 5
hit     = equipment.hit
dodge   = equipment.dodge
crit    = equipment.crit
resist  = equipment.resist
```

`jobPrimary` (2026-07-31):

```text
job 1 天机 → strength
job 2 幻剑 → agility
job 3 鬼道 → wisdom
```

See `2026-07-31-battle-attack-job-primary-attr.md`. Panel 物攻 remains
weapon-only (`item+0xFA`); this change is server battle authority only.

These feed battle start / hit rolls / soft mitigation only. Do not remap panel
halfwords to “力量→血、敏捷→甲、主属性→攻” without new client evidence;
mid-session level-up uses flat job rates, not those derivatives.

Rationale (battle model):

- equipment primary attributes are visible at full value on the panel path;
- weapon attack and armor fold into **server** combat at a reduced rate;
- HP/MP scale with level for persistence readability;
- hit / dodge / crit / resist are equipment-only (including enhance milestones);
- normal-attack damage scales with the job’s damage primary so agility / wisdom
  gear affects 幻剑 / 鬼道 auto-attacks the same way strength gear affects 天机.

## Defense Formula

Damage now uses a soft mitigation curve:

```text
damage = max(1, attack * 100 / (100 + defense))
```

Spell damage uses resist as a flat percent reduction (cap 70%):

```text
damage = raw * (100 - min(resist, 70)) / 100
```

命中后若减免到 0，进攻路径仍保底为 1；未命中保持 0。
This avoids the old `attack - defense` cliff where a small stat mismatch could
collapse damage to `1`, while still making defense meaningful at all levels.

## Skill Damage

Battle skills use `skill.dsh` instead of the normal attack value:

```text
base = abs(生命变化) for offensive rows
bonus = (strength * 力量系数 + agility * 敏捷系数 + wisdom * 智慧系数) / 100
raw = base + bonus
damage = max(base, raw * 100 / (100 + enemy_defense))
```

This keeps the DSH promise like `造成至少30点单体法术伤害` true while still letting
player attributes and monster defense matter. Normal attack remains
`vm_net_mock_battle_player_damage_to_enemy()`.

## Implementation Points

`src/mock-server.c` now centralizes the model in:

```text
vm_net_mock_load_equipment_catalog()
vm_net_mock_role_build_player_stats()
vm_net_mock_role_sync_derived_vitals()
vm_net_mock_damage_after_defense()
```

Call sites now using the model:

```text
vm_net_mock_build_actor_info()
vm_net_mock_build_battle_start_info_blob()
vm_net_mock_build_battle_scene_start_info_blob()
vm_net_mock_build_challenge_interaction_response()
vm_net_mock_battle_role_attack_default()
vm_net_mock_battle_role_defense_default()
vm_net_mock_battle_player_damage_to_enemy()
vm_net_mock_battle_player_skill_damage_to_enemy()
vm_net_mock_battle_enemy_damage_to_role()
vm_net_mock_role_apply_battle_settlement()
```

## Validation

Validated with:

```text
make
```

Result: passed.

`git diff --check` should still be run before finalizing a larger batch because
this repository emits Git CRLF conversion warnings for touched text files.
