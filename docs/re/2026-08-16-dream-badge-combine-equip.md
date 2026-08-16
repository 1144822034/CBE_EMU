# 梦境徽章合成梦境装备

日期：2026-08-16

## 需求

背包道具 `#840 梦境徽章`（`item.dsh` 已有）可合成梦境装备：

- 消耗：**300** 枚徽章 → **1** 件装备
- 装备池（`equip.dsh`）：`40065`..`40088`（共 24 件）
- **玩家自选**：点「合成」弹出列表，选定后再发请求（非随机）

## 契约（对齐玄晶碎片 29/11）

| 方向 | 内容 |
|------|------|
| 请求 | Pixi-only `1/29/12 { seq, destid }` |
| 成功 | `29/12` `result=1` + `itemid`/`destid`/`remaining`/`destcount` |
| 失败 | `2` 非徽章 / `3` 背包满 / `4` 不足 300 / `5` 装备目录缺失 / `6` 未选或目标不在池 |

装备不叠堆：发奖前需空格；若本堆恰好 300 枚，消耗后腾出一格再入装备。

## 修改

| 层 | 行为 |
|----|------|
| `mock_server_core.c` | `DREAM_BADGE_ITEM_ID=840`、`COMBINE_COST=300` |
| `mock_server_equipment_npc.c` | `build_dream_badge_combine_response` 校验 `destid` 池 |
| `mock_server_dispatch.c` | 挂接 29/12 |
| Pixi | 背包「合成」→ Dialog 列表（翻页）→ 带 `destid` 请求 |

## 验证

1. ≥300 梦境徽章 →「合成」→ 列表选一件 → 徽章 −300、获得所选装备；日志 `mock_dream_badge_combine ... dest=<所选> result=1`。
2. 取消列表：回背包，徽章数量不变。
3. 不足 300 / 背包满：行为与文档失败码一致。
4. 玄晶 / 玄晶碎片合成路径不受影响。
