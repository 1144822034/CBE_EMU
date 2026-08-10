# 2026-08-03 强化气血%/法力%结算基数

> 已被 `2026-08-05-hp-mp-percent-character-bare-base.md` 取代：% 只乘**人物裸池**，
> 不含装备定值；定值与 % 加值分别入 `equipment.hp/mp`。

## 变更（历史）

强化里程碑 `flag=1` 的气血%/法力%：

```text
percent = (品质 + 1) * 5   # wire 仍发此百分数；详情带 %
加值   = 人物裸血/裸蓝 * percentSum / 100   # 2026-08-05：不含定值
max    = 裸 + 定值 + 加值
```

人物裸蓝 = `70 + level*9 + baseWisdom*3`（不含装备智慧）。
## 修改点

- `mock_server_catalog.c`：`add_unlocked_milestones` 只累加 percent 点数
- `mock_server_role.c`：`collect_equipment_bonus` 在全部定值入账后按 `(裸+定值)×%` 结算

## 验证

- 属性页法力与战斗开局法力一致
- +12 品质4 详情仍为 +25%
- `make -j2`
