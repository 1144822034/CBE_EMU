# 气血%/法力% 按人物裸池结算

日期：2026-08-05

## 变更

强化 `flag=1` 的气血%/法力% 不再用装备 dsh 的 `item.bonus.hp/mp × percent`。

```text
bareHp = 90 + level×8 + endurance×2
bareMp = 70 + level×9 + baseWisdom×3
equipment.hp += bareHp × hpPercentSum / 100   # 定值里程碑仍绝对加
equipment.mp += bareMp × mpPercentSum / 100
maxHp / maxMp = bare + equipment.hp/mp
```

法力% 与生命（气血%）同一规则：基数是人物基础池，不是装备列。  
定值气血/法力里程碑（150/300/…）不变。详情 wire 仍发百分数。

## 修改点

- `mock_server_core.c`：`equipment_bonus` 增加 `hpPercent` / `mpPercent`
- `mock_server_catalog.c`：`add_unlocked_milestones` 累加百分数
- `mock_server_role.c`：`build_player_stats` 对裸池乘 % 后折入 `equipment.*`

## 验证

1. `make -j2`
2. 帽子 +12 品质4（法力 25%）：法力加值 ≈ `bareMp×25/100`，与面板/战斗一致
3. 披风/束腰气血%：加值 ≈ `bareHp×percent/100`
