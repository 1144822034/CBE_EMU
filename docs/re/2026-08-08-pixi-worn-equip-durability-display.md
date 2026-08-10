# 装备栏未显示服务端当前耐久

日期：2026-08-08

## 触发与现象

打开「装备信息」后，详情 `[耐久]` 始终为满值（如 `100/100`），打过折的装备也看不出损耗。

## 根因

拉数路径已正确：

- Pixi 打开装备栏 → `1/7/22` → `7/7 type=2`
- mock `equipment_login` blob：`count` = `serviceState->durability[slot]`（当前耐久）
- `parseWornEquipmentSnapshot` 已把该 u32 读进 `BagItem.count`

首次偏离在 UI：`equipDetail.buildEquipDetailLines` 写死
`durCur = durMax`，并注释「worn durability unresolved」，未使用服务端字段。

契约（`2026-07-29-enhance-attr-gray-and-unequip.md`）：

| 路径 | count 语义 |
| --- | --- |
| 背包 | 装备实例数量 1 |
| 穿戴 `7/7 type=2` | **当前耐久** |

## 修改

- `BagItem.durability?`：type=2 解析时写入 wire count
- `buildEquipDetailLines`：有 `durability` 时显示 `当前/上限`
- 背包装备耐久见 `2026-08-08-pixi-bag-equip-durability-display.md`

## 验证

1. 战后耐久下降 → 打开装备信息 → `[耐久]` 为当前值/上限。
2. 新装备满耐久显示正确。
3. 当前耐久为 0 的穿戴槽：红底 + 外圈脉动红光。
