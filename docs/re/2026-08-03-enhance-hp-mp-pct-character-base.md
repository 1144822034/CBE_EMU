# 2026-08-03 强化气血%/法力%结算基数

## 变更

强化里程碑 `flag=1` 的气血%/法力%：

```text
percent = (品质 + 1) * 5   # wire 仍发此百分数；详情带 %
定值池 = 人物裸血/裸蓝 + 装备气血/法力定值（dsh 列 + 定值里程碑）
加值   = 定值池 * percentSum / 100
max    = 定值池 + 加值
```

人物裸蓝 = `70 + level*9 + baseWisdom*3`（不含装备智慧）。

客户端 `0x0100FED8`：先累加定值进池，再 `percentSum * 池 / 100`。  
旧服务端只对裸池×% → 战斗法力比属性页少 `定值×%`（例：1759 vs 1908，Δ149≈定值×25%）。

多件 `%` 档的 percent **先求和**再乘一次池（与客户端一致），不是每件各自乘裸池后相加的另一种写法在 percent 相同时代数等价于求和；定值必须进池后再乘。

## 修改点

- `mock_server_catalog.c`：`add_unlocked_milestones` 只累加 percent 点数
- `mock_server_role.c`：`collect_equipment_bonus` 在全部定值入账后按 `(裸+定值)×%` 结算

## 验证

- 属性页法力与战斗开局法力一致
- +12 品质4 详情仍为 +25%
- `make -j2`
