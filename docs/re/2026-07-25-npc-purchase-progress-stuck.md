# NPC 商人购买成功后取数进度条卡住（2026-07-25）

## 症状

动态 NPC 药商购买后显示「购买成功」，取数进度条不消失。

## 已排除（运行时复测）

| 方案 | 日志特征 | 结果 |
|------|----------|------|
| 同包 `26/1+7/7` | `objects=2` | 卡住 |
| 购买只回 `26/1`，poll 投递 `7/7` | `objects=1` 后 `npc_purchase_backpack_deliver` | 卡住 |
| 成功 `options=0` | `options=0` | 仍卡住（只要带了 7/7） |
| 同包/同 poll 尾随 `26/0` | `objects=4` / deliver `objects=3` | 仍卡住 |

对照：`shop-category` 仅 `26/1 options=6`、无 `7/7` → 进度条正常消失。

## IDA 证据

`DispatchItemEvent(0x01039C28)`：kind=26 任意已处理 subtype（含 0/1）在分支末尾清
`r9+21808/21804`（基址字面量 `0x5520`）。`ResetDownloadState(0x0103993C)` 清同一对字段。

因此「购买成功」文案出现时，`26/1` 路径的 busy 清除已经执行过。

## 根因陈述

- **触发**：`shop-buy` 响应或紧随 poll 中出现 `1/7/7 type=1`（`mmGame:sub_11CE` /
  `TimerControl_ProcessItem`）。
- **违反契约**：物品增量事件可在 kind-26 busy 清除**之后**再次置位 `r9+21808`；
  与 `7/7` 同包/同事件的尾随 `26/0` 仍早于该置位完成，无法收尾。
- **首个错误状态**：busy 在物品增量处理后保持为 1，取数条不消失。
- **证据**：所有卡住复测都含 `mock_backpack_add`；不含 `7/7` 的 `shop-category`
  不卡；`0x01039C28` 证明 `26/1` 本身会清 busy。

## 修正契约

1. `shop-buy` 成功：**只**回 `1/26/1`，`option-count=0`，「购买成功。」（无 `7/7`）。
2. Scene poll **phase 1**（买后 ≥2 tick）：`1/7/7`（802/803 再加 `1/7/11`）。
3. Scene poll **phase 2**（phase1 后再 ≥2 tick）：**单独** `1/26/0`，再清 busy。

## 验证

```text
shop-buy ... options=0 objects=1 ...
npc_purchase_backpack_deliver ...   # 无 26/0
npc_purchase_busy_ack ... objects=1 # 单独 26/0
```

进度条应在 busy_ack 后消失；背包可见壶 count=1、储量为库值。
