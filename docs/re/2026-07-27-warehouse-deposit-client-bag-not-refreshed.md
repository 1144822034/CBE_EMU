# 仓库存入后客户端背包未刷新 / 网络超时（修订 7）

日期：2026-07-27

## 现象

- 壶 / 堆叠物存入后背包可正确消失（玄晶须走 remaining=0，勿 morph→802）。
- **装备**存入成功后仍显示在背包（像未刷新）。

## 根因

无 item-use pending 时，壶靠 `7/11=0` 删行。堆叠物（含玄晶）与强化/任务扣减相同：
`7/7 type=2` 写同 id `remaining` + `7/11`（可为 0）。旧版曾 morph→802，对玄晶会卡 busy。

对**装备行**，`mmGame:0xD04` 的 `7/7 type=2` 走 item-manager `+104` 且
固定传入 `r2=-1`，**不会改写背包行的 itemId**。

打开背包 / 退出仓库对话框在常见路径上**不发网包**（本地 UI），不能把刷新
挂在这些操作上。

## 修改（修订 7，2026-08-05 修订堆叠 peel）

| 物品 | phase 1（存入后下一拍 poll） |
|---|---|
| 802/803 | `7/11=0` |
| 普通/玄晶等堆叠 | `7/7 type=2` 同 id `remaining=0` + `7/11=0`（2026-08-05：不再 morph→802） |
| **装备** | **`17/1+7/42` 权威背包列表** |

phase 2：单独 `26/0`。

日志：

```text
mock_backpack_list_resync … equip=1 evidence=17/1+7/42-backpack-list-after-equip-deposit-then-26/0-…
backpack_resync_busy_ack …
```

玄晶 morph→802 卡住掉线见 `2026-08-05-warehouse-deposit-crystal-morph802-stuck.md`。

## 验证

1. 装备存入：服务端背包已无该行；poll 见 `17/1` / `mock_backpack_items`，再 `26/0`。
2. 壶 / 玄晶 / 普通堆叠存入后仍应在 poll 上立刻消失（无回归）。

## 已知边界

`17/1` 的权威解析器是 `mmGame:0x418C`（背包组件持有回调时）。在仓库
`scene_sync_poll` 上推送列表是按产品要求的即时下发；若主 item-manager 幽灵
仍可见，需另找场景可消费的删行契约（非打开背包 / 退出对话框）。

## 修订 8（已回退）

曾尝试装备存入后延迟 soft 同场景 `30/1`（仿商城退出重建 mmGame）。用户确认无效后已撤回，代码回到本修订 7。
