# Boss/首领主动释放技能（2026-07-30）

## 背景

`family=BOSS` 的怪物反击此前全部走普通攻击（actionType=0），没有技能特效与伤害放大，首领战偏“木桩普攻”。

## 契约

1. 反击仍走既有 `actioninfo` 反击位；命中/闪躲/暴击由
   `vm_net_mock_battle_enemy_damage_to_role` 先结算。
2. 仅 `vm_net_mock_monster_casts_active_skill(enemyId)` 为真时可升级为技能反击：
   actionType=1，`effectIndex` 取自 `skill.dsh` `技能图片`（列 2）。
   默认：无 MySQL 覆盖时 `family=BOSS` 开启；有覆盖时看后台 `cast_skill`
   （见 `2026-07-30-monster-admin-cast-skill.md`）。
3. 同一回合多怪反击时，仅 lead（strikeIndex=0）可施放技能，避免整排齐放。
4. 伤害在普攻结果上按倍率放大（默认 165%），不超过当前角色 HP。

## 修改

- `vm_net_mock_battle_resolve_enemy_counter_damage` /
  `vm_net_mock_battle_apply_enemy_counter_strike`
- 接入：道具回合、4/2 operate / fallback、pending enemy turn、
  组队 orphan round、逃跑失败反击
- 每条反击独立记录 actionType/effectIndex，避免复用玩家技能特效

## 环境变量

| 变量 | 默认 | 含义 |
|------|------|------|
| `CBE_BATTLE_BOSS_SKILL` | 1 | 0=关闭 |
| `CBE_BATTLE_BOSS_SKILL_CHANCE` | 45 | 施放概率（%） |
| `CBE_BATTLE_BOSS_SKILL_DAMAGE_PCT` | 165 | 伤害倍率 |
| `CBE_BATTLE_BOSS_SKILL_ID` | 0 | 固定技能；0=轮换 1/21/121/231/201 |

每第 3 个 armed turn 保底一次施放（在概率未命中时）。

## 验证

1. `make -j2 server`，重启服务端。
2. 打 `family=BOSS` 怪（如场景首领），观察反击偶发技能特效，伤害高于普攻。
3. 日志含 `mock_battle_boss_skill enemy=... skill=... effect=...`。
4. 普通怪仍为普攻反击；`CBE_BATTLE_BOSS_SKILL=0` 可关闭。
