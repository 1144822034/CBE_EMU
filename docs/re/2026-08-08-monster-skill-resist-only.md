# 怪物技能对玩家：只看法抗、不看护甲

日期：2026-08-08

## 契约

| 路径 | 减伤 |
|------|------|
| 怪物普攻 → 玩家 | 护甲软曲线 `damage_after_defense` |
| 怪物技能 → 玩家 | **仅法抗** `damage_after_resist`（上限 70%），忽略护甲 |

## 修改

- `vm_net_mock_battle_enemy_skill_remitigate_to_role`：命中后按 `playerStats.resist` 重算
- `mock_battle_resolve_enemy_counter_damage`：技能倍率加在法抗减伤后的底数上

## 验证

日志：`mock_battle_enemy_skill_mitigate … resist=… defense_ignored` 或
`evidence=monster-skill-resist-only`；同场怪物普攻伤害仍随护甲变化。
