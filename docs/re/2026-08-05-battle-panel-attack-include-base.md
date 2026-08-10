# 战斗/面板物攻统一走 build_player_stats（含基础主属性）

日期：2026-08-05

## 契约

`vm_net_mock_role_build_player_stats` 为唯一聚合入口：

```text
attack = jobPrimary(baseStrength|Agility|Wisdom + equip) + equipment.attack
         (cap 65535)
```

| 职业 | jobPrimary |
| --- | --- |
| 天机 | strength |
| 幻剑 | agility |
| 鬼道 | wisdom |

- 战斗：`battle_role_stats_current` → `build_player_stats` + 战斗 buff（主属性 Δ 全额带动 attack）
- 面板 overlay：`panelAtk = stats->attack`（不再只发武器 `equipment.attack`）

## 修改前偏离

代码仍停在旧式 `6+level*2+primary/2+equipAtk/3`，buff 也只跟 `primary/2`；overlay 只发装攻，不含人物基础主属性。

## 修改

1. `mock_server_role.c`：`stats->attack` 改为上式；buff `Δattack = ΔjobPrimary`
2. `mock_server_recovered_helpers.inc`：overlay 物攻改用 `stats->attack`

## 护甲（同日追加）

`defense = 4 + level + endurance/2 + equipment.armor`（裸 + 全额装甲；面板显示全额）。

面板 overlay / actorinfo 护甲字同源。**普攻减伤**自 2026-08-10 起改为
`bare + equipment.armor/5` 且软曲线 `K=2000`，见
`2026-08-10-na-defense-scale-and-soft-curve-k.md`。

## 验证

1. `make -j2`
2. 面板护甲 ≈ 裸防 + 全额装甲
3. 穿脱防具后 overlay / 战斗承伤同向变化（战斗侧按装甲 /5）
4. 2026-08-10：万级护甲下普攻不再塌成两位数