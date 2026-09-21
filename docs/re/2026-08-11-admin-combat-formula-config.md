# 战斗公式管理后台可配置

## 范围

服务端战斗结算（普攻软护甲、命中/闪躲、暴击、抗性、技能加成、决斗浮动、怪物副属性默认、Boss 技能）改为 MySQL 单例配置，管理后台可热改。PixiJS 客户端仍只回放 `actioninfo.valueA` / `child_flag`，不参与公式计算。

## 入口

- 管理页：`http://127.0.0.1:19091/?tab=combat`
- 表：`server_combat_formula_config`（`id=1`）
- 迁移：`server/mysql/migrate_add_combat_formula_config.sql`；
  主属性物攻系数追加：`server/mysql/migrate_add_primary_attack_coeff.sql`；
  力量→血 / 智慧→法追加：`server/mysql/migrate_add_panel_str_hp_wis_mp_coeff.sql`
  （运行时也会 `ALTER` 补列）
- 运行时也会 `CREATE TABLE IF NOT EXISTS` 并 `INSERT IGNORE` 默认行

## 默认值（与改前硬编码一致）

| 项 | 默认 | 用途 |
|----|------|------|
| panel_defense_base / level_coeff / endurance_div | 4 / 1 / 2 | `defense = base + level*coeff + endurance/div + equip.armor` |
| panel_hit_base | 101 | `hit = base + equip.hit` |
| panel_strength_hp_coeff_x100 | 50 | `bareHp += strength × x100/100`（50=0.5；含装备力量） |
| panel_wisdom_mp_coeff_x100 | 50 | `bareMp += wisdom × x100/100`（50=0.5；在成长智慧×3 之外；含装备智慧） |
| primary_attack_coeff_* | 天机100 / 幻剑100 / 鬼道100 | `attack = jobPrimary×coeff%/100 + equip.attack` |
| soft_armor_k | 2000 | 普攻软护甲 `damage=(atk*K+def/2)/(K+def)` |
| equip_armor_divisor | 5 | 战斗减伤用 `bare + equip.armor/div` |
| hit_chance_min / max | 5 / 95 | `clamp(hit-dodge)` |
| crit_damage_percent | 150 | 暴击倍率 |
| resist_cap_percent | 70 | 抗性减伤硬顶 |
| job_skill_attack_bonus_percent | 5 | PvE 技能职业加成 |
| na / skill_variance_percent | 5 / 3 | 普攻/技能伤害浮动 ±%（PvE + 决斗，暴击前） |
| duel_job_bonus_* | 天机10 / 幻剑8 / 鬼道12 | 决斗主属性加成 |
| skill_vs_na_floor_percent | 90 | 单体技能相对普攻保底 |
| monster_*_default | hit100 / 其余0 | 怪物副属性；仍可被 `CBE_BATTLE_MONSTER_*` 覆盖 |
| boss_skill_chance / damage_percent | 45 / 300 | Boss 反击；仍可被 `CBE_BATTLE_BOSS_SKILL_*` 覆盖 |

## 代码落点

- 配置缓存 / 读写：`src/server/mock_server_combat_formula_config.inc.c`（由 `mock_server_role.c` include）
- 公式消费：`mock_server_role.c`、`mock_server_recovered_helpers.inc`、`mock_server_battle.c`
- 后台页：`src/web_admin_combat.inc.c`

## 验证

1. 启动服务后打开 `?tab=combat`，确认默认值与上表一致。
2. 改 `soft_armor_k` / `crit_damage_percent` 保存，日志应出现 `combat_formula_config_load`，下一场战斗伤害变化与参数一致。
3. 设置 `CBE_BATTLE_MONSTER_HIT` 后怪物命中仍走环境变量覆盖。

## unresolved

- 原版 CBE 真实命中/暴击倍率/抗性曲线仍未从客户端反证确认；后台改的是当前 provisional 服务端契约，不是已还原的官方公式。
