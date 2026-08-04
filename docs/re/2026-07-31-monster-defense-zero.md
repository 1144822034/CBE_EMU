# 怪物护甲战斗结算

## 2026-07-31（已撤销）

曾与抗性同策略：玩家打怪时强制 `defense=0`。2026-08-03 起恢复目录/后台防御。

## 当前契约（2026-08-03）

| 路径 | 默认减伤属性 | 覆盖 env |
| --- | --- | --- |
| 普攻 `vm_net_mock_battle_player_damage_to_enemy` | `stats.defense` | `CBE_BATTLE_ENEMY_DEFENSE` |
| 进攻技能（物理+法术） | 次级抗性（默认 0） | `CBE_BATTLE_SKILL_ENEMY_RESIST` / `CBE_BATTLE_MONSTER_RESIST` |

- 减甲 ailment 仍可叠在普攻防御基值上；技能走抗性 ailment。
- 玩家受击 / 切磋普攻仍用 `defense`；切磋技能用对方 `resist`。
- 普攻：`damage_after_defense` 软减伤 + pierce；技能：`damage_after_resist`（上限 70%）。

## 验证

- `make -j2`
- 同攻击打高防怪：普攻飘字应低于打低防怪；`CBE_BATTLE_ENEMY_DEFENSE=0` 可临时退回无护甲对照。
- 物技：改抗性后不再吃怪防；`CBE_BATTLE_MONSTER_RESIST=30` 应约保留 70% raw。
