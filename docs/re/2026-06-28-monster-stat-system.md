# 2026-06-28 Monster Stat System

## Phase

Build a systematic monster stat layer for the local Jianghu OL mock server.

The goal is no longer a single hard-coded poison slime. The mock server now
knows every monster id referenced by local automatic monster resources, assigns
each one a level/family, and derives HP, MP, attack, defense, EXP, and copper
from one RPG-style rule set.

## Evidence

Local resource evidence:

- `bin/JHOnlineData/automonster.dsh`
  - Columns: `场景`, `怪物id1`, `怪物id2`, `怪物id3`.
  - Rows: 104 scene rows.
  - Unique nonzero monster ids: 73.
- `bin/JHOnlineData/task.dsh`
  - Columns include task target text plus structured requirement fields:
    `任务要求类型1`, `任务要求ID1`, and `任务要求数量1`.
  - `任务要求类型1 = 1` identifies task material item requirements. The
    item id/name is structured, but the source monster is usually only in the
    task text. There is no drop probability column.
- `bin/JHOnlineData/item.dsh`
  - Task material items such as `18 蝙蝠翅膀`, `19 瘴气毛发`, and
    `25 稻谷` are category `11`, stack `30`.
- Resource search notes:
  - `tempData.bin` contains the same `automonster.dsh` scene/monster table.
  - `mmBattleMstarWqvga.cbm` contains "掉落/背包/物品说明/拾取/丢弃" UI
    strings, not a monster drop probability table.
  - Extracted resource package file names include `item.dsh`, `task.dsh`,
    `automonster.dsh`, etc.; no separate monster drop/probability DSH was found.
- `tmp/all_sce_bundle/<scene>.sce/scene.json`
  - Monster spawn records are exported as `records[].unknown_blob`.
  - The combat spawn token shape is:

```text
u16 kind = 3
u16 x
u16 y
meta token type 5 or 6: field 14 = actor id
string field 15 = display name
scalar field 16 = visual/class hint
string field 17 = actor resource
```

IDA parser boundary:

- `mmBattleMstarWqvga.cbm:HandleBattleStartMsg(0x66CC)` consumes `side` and
  `battleinfo`.
- `mmBattleMstarWqvga.cbm:HandleBattleActionMsg(0x6EB0)` consumes
  `actionnum/actioninfo`.
- `mmBattleMstarWqvga.cbm:HandleBattleSettleMsg(0x743C)` consumes settlement
  fields including `exp`, `gold`, `hp`, and `mp`.

Runtime/server boundary:

- Scene-monster battle start subtype 5 copies monster HP/MP from the scene node
  seeded by `1/2/2 { moveinfo }`, specifically raw blob offsets `+44/+48/+52/+56`.
- Battle action damage is expressed through subtype `1/4/6` action child value A
  as a negative HP delta.
- Battle reward persistence and display are expressed through subtype `1/4/7`.

## Implementation

`src/mock-server.c` now has:

- `g_vm_net_mock_monster_entries[]`: 73 local automatic monster ids plus 4
  task-only structured monster ids, with server level and monster family.
- `vm_net_mock_monster_stats_for_enemy()`: derives HP, MP, attack, defense, EXP,
  copper, and optional drop info from the monster entry.
- `vm_net_mock_battle_player_damage_to_enemy()`: player attack from the unified
  player attribute model against monster defense.
- `vm_net_mock_battle_enemy_damage_to_role()`: monster attack against player
  defense from the unified player attribute model.
- `vm_net_mock_damage_after_defense()`: soft mitigation
  `(attack*100+def/2)/(100+def)`, floor at `attack*35/100` (armor ≤65%
  block), then if `def/2 < attack` add `(attack - def/2) * 2`.

The existing environment overrides remain useful for focused experiments:

```text
CBE_BATTLE_ENEMY_HP
CBE_BATTLE_ENEMY_MAX_HP
CBE_BATTLE_ENEMY_MP
CBE_BATTLE_ENEMY_MAX_MP
CBE_BATTLE_PLAYER_ATTACK
CBE_BATTLE_ENEMY_ATTACK
CBE_BATTLE_ENEMY_DEFENSE
CBE_BATTLE_ROLE_DEFENSE
CBE_BATTLE_REWARD_EXP
CBE_BATTLE_REWARD_GOLD
CBE_BATTLE_DROP_ITEM_ID
CBE_BATTLE_DROP_RATE
```

Poison slime compatibility remains explicit:

```text
enemy_id = 105
name = 毒泥怪
hp/mp = 20/20
attack/defense = 8/2
reward = 5 EXP, 5 copper
drop = 小长命散 item 304, 10%
```

## Drop Evidence

No recovered resource currently provides a general
`monster id -> item id -> probability` table. The active drop entries are
therefore split by evidence type:

- Poison slime `105` keeps the user-requested ordinary battle reward:
  `304 小长命散`, `10%`.
- Task material drops come from `task.dsh` item requirements and matching
  monster names/ids. Since no probability field exists in the resources, the
  mock uses the explicit server design constant
  `VM_NET_MOCK_TASK_MATERIAL_DROP_RATE = 100`.
- Unmatched task material rows, such as task text that names a boss but does not
  expose a stable actor id in the current mock path, are not added to the table.

Current task-material mappings added to `g_vm_net_mock_monster_entries[]`:

| Monster ID | Evidence | Drop item | Rate |
| -: | - | - | -: |
| 1 | task.dsh 野猪 -> 黑猪獠牙 | 27 黑猪獠牙 | 100% |
| 3 | task.dsh 桃花岛西侧桃林 -> 蝙蝠翅膀 | 18 蝙蝠翅膀 | 100% |
| 6 | task.dsh 老野猪 -> 老野猪皮 | 29 老野猪皮 | 100% |
| 9 | task.dsh 掘地鼠 -> 稻谷 | 25 稻谷 | 100% |
| 13 | task.dsh 白灵蟒 -> 白蟒毒牙 | 32 白蟒毒牙 | 100% |
| 15 | task.dsh 碧竹巨蛇 structured monster id | 34 碧竹蛇胆 | 100% |
| 18 | task.dsh 岩浆石火 -> 怒焰火石 | 36 怒焰火石 | 100% |
| 19 | task.dsh 岳老三 -> 家传铁锤 | 37 家传铁锤 | 100% |
| 22 | task.dsh 碧纱幽火 -> 幽火结晶 | 53 幽火结晶 | 100% |
| 25 | task.dsh 恶臭僵尸 -> 臭鸡蛋 | 43 臭鸡蛋 | 100% |
| 28 | task.dsh 灰蝙蝠 -> 灰色晶石 | 45 灰色晶石 | 100% |
| 30 | task.dsh 雪岩怪 -> 冷水晶 | 47 冷水晶 | 100% |
| 34 | task.dsh 碎骨兵 -> 骨头碎片 | 51 骨头碎片 | 100% |
| 36 | task.dsh 青石怪 -> 青石怪的心 | 52 青石怪的心 | 100% |
| 41 | task.dsh 恶臭胶质 -> 恶灵胶 | 55 恶灵胶 | 100% |
| 42 | task.dsh 白焰鬼 -> 鬼焰 | 56 鬼焰 | 100% |
| 45 | task.dsh 红鬼火 -> 鬼火 | 58 鬼火 | 100% |
| 47 | task.dsh 持刀日月教徒 -> 日月教徒头巾 | 63 日月教徒头巾 | 100% |
| 49 | task.dsh 杂斑野猪 -> 野猪獠牙 | 68 野猪獠牙 | 100% |
| 50 | task.dsh 金蜜蜂 -> 金蜜蜂 | 71 金蜜蜂 | 100% |
| 51 | task.dsh 蓝灯鬼 -> 蓝灯鬼的魂魄 | 69 蓝灯鬼的魂魄 | 100% |
| 53 | task.dsh 白鳞/白磷蛇 -> 白磷蛇干 | 67 白磷蛇干 | 100% |
| 54 | task.dsh 碧竹蛇 -> 碧竹蛇毒 | 60 碧竹蛇毒 | 100% |
| 57 | task.dsh 长枪鬼卒 -> 忘忧草 | 61 忘忧草 | 100% |
| 60 | task.dsh 碎颅/碎骨骷髅 -> 颅骨碎片 | 66 颅骨碎片 | 100% |
| 63 | task.dsh 青龙王 structured monster id | 35 青龙明珠 | 100% |
| 64 | task.dsh 火焰山日月教徒 -> 日月令符 | 62 日月令符 | 100% |
| 65 | task.dsh 东方不败 structured monster id | 38 一盒胭脂 | 100% |
| 67 | task.dsh 拿枪士兵 -> 士兵护符 | 64 士兵护符 | 100% |
| 69 | task.dsh 腐血骸骨 -> 灵骸 | 70 灵骸 | 100% |
| 77 | task.dsh 丹霞山巴寨/染血骷髅 -> 血骨片 | 28 血骨片 | 100% |
| 78 | task.dsh 死骨锤兵 -> 骨锤之骨 | 50 骨锤之骨 | 100% |
| 79 | task.dsh 青苔岩精 -> 青苔岩的精华 | 49 青苔岩的精华 | 100% |
| 92 | task.dsh 暗紫尸骸 -> 白骨碎片 | 57 白骨碎片 | 100% |
| 94 | task.dsh 黄色蜂群 -> 蜂皇浆 | 59 蜂皇浆 | 100% |
| 97 | task.dsh 赤焰怪 -> 赤焰怪牙 | 65 赤焰怪牙 | 100% |
| 106 | task.dsh 瘴气蝙蝠 -> 瘴气毛发 | 19 瘴气毛发 | 100% |
| 202 | task.dsh 炼狱鬼兵 -> 雪莲花 | 80 雪莲花 | 100% |

## Stat Rules

Monster levels are based on first appearance in `automonster.dsh` row order,
with a +1 offset when a monster first appears in the second/third monster slot.
Families then derive stats by formula:

```text
slime:     hp=16+L*4,  mp=18+L*2, attack=6+L*2,  defense=2+L/4, exp=3+L*2, gold=3+L*2
beast:     hp=26+L*7,  mp=10+L,   attack=7+L*2,  defense=2+L/3, exp=4+L*3, gold=3+L*2
flying:    hp=18+L*5,  mp=12+L,   attack=8+L*2,  defense=1+L/4, exp=4+L*3, gold=3+L*2
insect:    hp=16+L*5,  mp=12+L,   attack=8+L*2,  defense=1+L/5, exp=4+L*3, gold=3+L*2
reptile:   hp=24+L*6,  mp=14+L,   attack=8+L*2,  defense=2+L/3, exp=5+L*3, gold=4+L*2
undead:    hp=34+L*8,  mp=10+L,   attack=8+L*2,  defense=4+L/3, exp=6+L*3, gold=4+L*2
spirit:    hp=22+L*6,  mp=16+L*3, attack=10+L*2, defense=2+L/3, exp=6+L*3, gold=5+L*2
elemental: hp=30+L*7,  mp=20+L*4, attack=11+L*2, defense=3+L/3, exp=7+L*3, gold=5+L*2
stone:     hp=42+L*9,  mp=12+L*2, attack=8+L*2,  defense=6+L/2, exp=8+L*3, gold=5+L*2
humanoid:  hp=30+L*7,  mp=12+L*2, attack=9+L*2,  defense=3+L/3, exp=6+L*3, gold=6+L*2
soldier:   hp=34+L*8,  mp=10+L,   attack=10+L*2, defense=4+L/3, exp=7+L*3, gold=7+L*2
boss:      hp=80+L*12, mp=24+L*4, attack=14+L*3, defense=8+L/2, exp=20+L*5, gold=25+L*4
```

## Monster Table

| ID | 名称 | 首次场景 | 等级 | 类型 | HP | MP | 攻击 | 防御 | 经验 | 铜钱 | 掉落 |
| - | - | - | -: | - | -: | -: | -: | -: | -: | -: | - |
| 1 | 野猪 | 06野猪林_01.sce | 6 | beast | 68 | 16 | 19 | 4 | 22 | 15 | - |
| 3 | 黑蝙蝠 | 01桃花岛_02.sce | 1 | flying | 23 | 13 | 10 | 1 | 7 | 5 | - |
| 4 | 黄色蜜蜂 | 01桃花岛_03.sce | 2 | insect | 26 | 14 | 12 | 1 | 10 | 7 | - |
| 6 | 老野猪 | 06野猪林_03.sce | 7 | beast | 75 | 17 | 21 | 4 | 25 | 17 | - |
| 9 | 掘地鼠 | 03丹霞山_02.sce | 3 | beast | 47 | 13 | 13 | 3 | 13 | 9 | - |
| 13 | 白灵蟒 | 09华山_03.sce | 12 | boss | 224 | 72 | 50 | 14 | 80 | 73 | - |
| 18 | 岩浆石火 | 22火焰山_04.sce | 38 | elemental | 296 | 172 | 87 | 15 | 121 | 81 | - |
| 19 | 岳老三 | 17无情谷_03.sce | 28 | boss | 416 | 136 | 98 | 22 | 160 | 137 | - |
| 22 | 碧纱幽火 | 10封神坛_01.sce | 12 | spirit | 94 | 52 | 34 | 6 | 42 | 29 | - |
| 25 | 恶臭僵尸 | 05上古皇陵_02.sce | 4 | undead | 66 | 14 | 16 | 5 | 18 | 12 | - |
| 28 | 灰蝙蝠 | 06野猪林_02.sce | 7 | flying | 53 | 19 | 22 | 2 | 25 | 17 | - |
| 29 | 鬼蝙蝠 | 06野猪林_04.sce | 8 | flying | 58 | 20 | 24 | 3 | 28 | 19 | - |
| 30 | 雪岩怪 | 07泰山_01.sce | 8 | stone | 114 | 28 | 24 | 10 | 32 | 21 | - |
| 31 | 苗人教徒 | 07泰山_02.sce | 9 | humanoid | 93 | 30 | 27 | 6 | 33 | 24 | - |
| 32 | 灰疫鼠 | 09华山_01.sce | 10 | beast | 96 | 20 | 27 | 5 | 34 | 23 | - |
| 34 | 碎骨兵 | 09华山_02.sce | 11 | undead | 122 | 21 | 30 | 7 | 39 | 26 | - |
| 36 | 青石怪 | 10封神坛_05.sce | 14 | stone | 168 | 40 | 36 | 13 | 50 | 33 | - |
| 40 | 碧磷鬼火 | 13剑阁_02.sce | 20 | spirit | 142 | 76 | 50 | 8 | 66 | 45 | - |
| 41 | 恶臭胶质 | 13剑阁_01.sce | 20 | slime | 96 | 58 | 46 | 7 | 43 | 43 | - |
| 42 | 白焰鬼 | 15剑冢_01.sce | 22 | elemental | 184 | 108 | 55 | 10 | 73 | 49 | - |
| 45 | 红鬼火 | 15剑冢_02.sce | 22 | spirit | 154 | 82 | 54 | 9 | 72 | 49 | - |
| 47 | 持刀日月教徒 | 17无情谷_02.sce | 27 | humanoid | 219 | 66 | 63 | 12 | 87 | 60 | - |
| 48 | 长矛兵 | 17无情谷_04.sce | 28 | soldier | 258 | 38 | 66 | 13 | 91 | 63 | - |
| 49 | 杂斑野猪 | 20黑龙潭_03.sce | 31 | beast | 243 | 41 | 69 | 12 | 97 | 65 | - |
| 50 | 金蜜蜂 | 20黑龙潭_01.sce | 30 | insect | 166 | 42 | 68 | 7 | 94 | 63 | - |
| 51 | 蓝灯鬼 | 20黑龙潭_02.sce | 31 | spirit | 208 | 109 | 72 | 12 | 99 | 67 | - |
| 52 | 赤炼蛇 | 19金蛇谷_01.sce | 28 | reptile | 192 | 42 | 64 | 11 | 89 | 60 | - |
| 53 | 白鳞蛇 | 19金蛇谷_03.sce | 29 | reptile | 198 | 43 | 66 | 11 | 92 | 62 | - |
| 54 | 碧竹蛇 | 19金蛇谷_02.sce | 29 | reptile | 198 | 43 | 66 | 11 | 92 | 62 | - |
| 55 | 死刀兵 | 21幽冥鬼府_04.sce | 34 | undead | 306 | 44 | 76 | 15 | 108 | 72 | - |
| 56 | 巨锤骷髅 | 21幽冥鬼府_05.sce | 34 | undead | 306 | 44 | 76 | 15 | 108 | 72 | - |
| 57 | 长枪鬼卒 | 21幽冥鬼府_01.sce | 32 | undead | 290 | 42 | 72 | 14 | 102 | 68 | - |
| 60 | 碎颅骷髅 | 22火焰山_02.sce | 36 | undead | 322 | 46 | 80 | 16 | 114 | 76 | - |
| 64 | 日月教徒 | 22火焰山_03.sce | 37 | humanoid | 289 | 86 | 83 | 15 | 117 | 80 | - |
| 67 | 拿枪士兵 | 17无情谷_03.sce | 27 | soldier | 250 | 37 | 64 | 13 | 88 | 61 | - |
| 69 | 腐血骸骨 | 16锁妖塔_05.sce | 24 | undead | 226 | 34 | 56 | 12 | 78 | 52 | - |
| 70 | 阴风鬼灯 | 16锁妖塔_03.sce | 24 | spirit | 166 | 88 | 58 | 10 | 78 | 53 | - |
| 71 | 鬼岩石 | 16锁妖塔_02.sce | 23 | stone | 249 | 58 | 54 | 17 | 77 | 51 | - |
| 73 | 上古遗骸 | 05上古皇陵_03.sce | 5 | undead | 74 | 15 | 18 | 5 | 21 | 14 | - |
| 74 | 箭簇骷髅 | 05上古皇陵_04.sce | 5 | undead | 74 | 15 | 18 | 5 | 21 | 14 | - |
| 75 | 骸骨禁卫 | 05上古皇陵_05.sce | 6 | undead | 82 | 16 | 20 | 6 | 24 | 16 | - |
| 76 | 饿虎 | 03丹霞山_01.sce | 3 | beast | 47 | 13 | 13 | 3 | 13 | 9 | - |
| 77 | 染血骷髅 | 03丹霞山_03.sce | 4 | undead | 66 | 14 | 16 | 5 | 18 | 12 | - |
| 78 | 死骨锤兵 | 07泰山_03.sce | 9 | undead | 106 | 19 | 26 | 7 | 33 | 22 | - |
| 79 | 青苔岩精 | 09华山_03.sce | 11 | stone | 141 | 34 | 30 | 11 | 41 | 27 | - |
| 81 | 灯焰鬼 | 10封神坛_03.sce | 13 | spirit | 100 | 55 | 36 | 6 | 45 | 31 | - |
| 82 | 黑斑虎 | 11终南山_01.sce | 15 | beast | 131 | 25 | 37 | 7 | 49 | 33 | - |
| 83 | 白光虎 | 11终南山_02.sce | 15 | beast | 131 | 25 | 37 | 7 | 49 | 33 | - |
| 84 | 幽蓝鬼火 | 11终南山_03.sce | 17 | spirit | 124 | 67 | 44 | 7 | 57 | 39 | - |
| 86 | 墓地士兵 | 12活死人墓_01.sce | 17 | soldier | 170 | 27 | 44 | 9 | 58 | 41 | - |
| 87 | 流窜苗人 | 12活死人墓_02.sce | 17 | humanoid | 149 | 46 | 43 | 8 | 57 | 40 | - |
| 89 | 近卫士兵 | 12活死人墓_05.sce | 18 | soldier | 178 | 28 | 46 | 10 | 61 | 43 | - |
| 91 | 蜜蜂群 | 13剑阁_03.sce | 21 | insect | 121 | 33 | 50 | 5 | 67 | 45 | - |
| 92 | 暗紫尸骸 | 15剑冢_03.sce | 23 | slime | 108 | 64 | 52 | 7 | 49 | 49 | - |
| 94 | 黄色蜜蜂群 | 17无情谷_01.sce | 26 | insect | 146 | 38 | 60 | 6 | 82 | 55 | - |
| 97 | 赤焰怪 | 22火焰山_01.sce | 36 | elemental | 282 | 164 | 83 | 15 | 115 | 77 | - |
| 98 | 金毛太岁 | 23蟠龙寨_01.sce | 38 | slime | 168 | 94 | 82 | 11 | 79 | 79 | - |
| 99 | 白光虎 | 23蟠龙寨_04.sce | 39 | beast | 299 | 49 | 85 | 15 | 121 | 81 | - |
| 101 | 日月教徒 | 23蟠龙寨_05.sce | 39 | humanoid | 303 | 90 | 87 | 16 | 123 | 84 | - |
| 103 | 拿枪教徒 | 23蟠龙寨_07.sce | 40 | humanoid | 310 | 92 | 89 | 16 | 126 | 86 | - |
| 104 | 近卫教徒 | 23蟠龙寨_08.sce | 41 | humanoid | 317 | 94 | 91 | 16 | 129 | 88 | - |
| 105 | 毒泥怪 | 01桃花岛_01.sce | 1 | slime | 20 | 20 | 8 | 2 | 5 | 5 | 小长命散 10% |
| 106 | 瘴气蝙蝠 | 01桃花岛_04.sce | 2 | flying | 28 | 14 | 12 | 1 | 10 | 7 | - |
| 107 | 蓝鬼灯 | 05上古皇陵_03.sce | 6 | spirit | 58 | 34 | 22 | 4 | 24 | 17 | - |
| 110 | 紫云鬼 | 27深渊沼泽_01.sce | 46 | stone | 456 | 104 | 100 | 29 | 146 | 97 | - |
| 111 | 蓝云鬼 | 27深渊沼泽_03.sce | 48 | stone | 474 | 108 | 104 | 30 | 152 | 101 | - |
| 112 | 绿云鬼 | 27深渊沼泽_06.sce | 50 | stone | 492 | 112 | 108 | 31 | 158 | 105 | - |
| 120 | 地府魔蝎 | 28地府门道_01.sce | 50 | reptile | 324 | 64 | 108 | 18 | 155 | 104 | - |
| 121 | 怒火魔头 | 28地府门道_02.sce | 52 | elemental | 394 | 228 | 115 | 20 | 163 | 109 | - |
| 122 | 封印僵尸 | 28地府门道_04.sce | 53 | undead | 458 | 63 | 114 | 21 | 165 | 110 | - |
| 200 | 炼狱鬼火 | 25华山洞窟_01.sce | 43 | spirit | 280 | 145 | 96 | 16 | 135 | 91 | - |
| 201 | 炼狱骷髅 | 25华山洞窟_03.sce | 44 | undead | 386 | 54 | 96 | 18 | 138 | 92 | - |
| 202 | 炼狱鬼兵 | 25华山洞窟_04.sce | 45 | undead | 394 | 55 | 98 | 19 | 141 | 94 | - |

## Validation

Build:

```text
make
result: passed
```

## Unknowns

- There is still no recovered live probability table. Current task material
  rates are server-side modeling based on `task.dsh` item requirements, not a
  recovered live-server probability value.
- Some task text names special monsters whose actor id is not yet recovered in
  the current mock battle path; those rows remain intentionally absent instead
  of guessing an id.
- Player skill formulas are not yet recovered. The current normal attack uses
  the server-side player attribute model documented in
  `2026-06-28-player-attribute-model.md`.
