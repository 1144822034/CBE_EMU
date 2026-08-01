# 怪物护甲战斗结算为 0（2026-07-31）

## 变更

与 `2026-07-30-monster-resist-zero.md` 同策略：玩家打怪时**不再吃怪物护甲**。

| 路径 | 原 fallback | 现 fallback | 覆盖 env |
| --- | --- | --- | --- |
| 普攻 `vm_net_mock_battle_player_damage_to_enemy` | `stats.defense` | **0** | `CBE_BATTLE_ENEMY_DEFENSE` |
| 物理技能 `vm_net_mock_battle_player_skill_damage_to_enemy` | `monsterStats.defense` | **0** | `CBE_BATTLE_SKILL_ENEMY_DEFENSE` |

- 家族/后台表里的 `defense` 仍保留，供管理端与日志对照。
- 玩家受击仍用玩家护甲；切磋 PvP 仍用对方 `defense`。
- 减甲 ailment 仍叠在该 0 基值上（负向无效，正向可临时抬防御）。

## 验证

- `make -j2`
- 同攻击对同怪：飘字应接近 raw（`damage_after_defense(atk, 0)` ≈ atk），除非 env 显式设了防御。
- `CBE_BATTLE_ENEMY_DEFENSE=50` 可临时恢复软减伤。
