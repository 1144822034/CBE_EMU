# 战斗普攻改吃职业主属性（2026-07-31）

## 触发与反馈

1. 幻剑穿戴加敏捷装备：面板物攻不变，普攻伤害也不涨。
2. 天机部分单体技能（低阶万剑 / 破甲）结算低于普攻（另案：技能不吃武器攻击）。

## 链路

```text
equip.dsh 力/敏/智列
  → vm_net_mock_role_collect_equipment_bonus
  → vm_net_mock_role_build_player_stats (strength/agility/wisdom)
  → stats->attack
  → vm_net_mock_battle_player_damage_to_enemy
```

技能伤害仍走 `skill.dsh` 系数，不经过 `stats->attack`。

## 第一次偏离

旧公式：

```text
attack = 6 + level * 2 + strength / 2 + equipment.attack / 3
```

对所有职业都用力量。幻剑主成长与技能系数是敏捷，鬼道是智慧，因此加敏/智装备只抬技能系数项、不抬普攻。

面板物攻仍是客户端契约（仅武器 `item+0xFA`），本轮不改面板。

## 根因

服务端战斗 `attack` 未按职业主属性选型；与 `skill.dsh`「天机力 / 幻剑敏 / 鬼道智」不一致。

## 修改

`vm_net_mock_role_job_primary_attr`：

| role job | 职业 | primary |
| ---: | --- | --- |
| 1 | 天机 | strength |
| 2 | 幻剑 | agility |
| 3 | 鬼道 | wisdom |

```text
attack = 6 + level * 2 + jobPrimary / 2 + equipment.attack / 3
```

战斗内 buff 对主属性的 `/2` 派生同步改为跟 `jobPrimary`（不再固定力量）。

文件：`src/server/mock_server_role.c`；模型说明：`2026-06-28-player-attribute-model.md`。

## 未改

- 面板物攻 halfword（仍仅武器）。
- 技能公式（仍 `base + Σ attr×coeff/100`，不含 `equipment.attack`）。
- 怪物 ailment 里 `strength/2` 叠到怪攻（怪物无职业主属性）。

## 验证

- `make -j2`
- 幻剑：穿纯敏防具（如皮甲 `敏捷变化>0`、`攻击=0`）→ 同目标普攻飘字应上升约 `Δ敏/2` 再经防御曲线。
- 天机：只加力量装 → 普攻仍涨；只加敏捷装 → 普攻不应因敏上涨。
- 鬼道：加智慧装 → 普攻上涨；技能仍主要吃智慧系数。
- 战斗内加力/敏/智 buff：仅当前职业主属性的变化应带动 `attack` 的 `/2` 派生。

## 残留风险

- 无原始服务端公式证据；本轮是产品约定对齐职业主属性。
- 鬼道普攻吃智慧后，高智角色普攻会明显抬升；若需「法师普攻弱」需另定契约。
- 天机技能仍可能低于带高 `equipment.attack` 的普攻（技能不含武器攻击）。
