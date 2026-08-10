# Pixi 神力丸/大力丸「使用无响应」

日期：2026-08-08

## 触发与现象

背包使用 830 神力丸（或 829 大力丸）时提示「使用无响应」，增益未生效。

## 根因

CBE 契约为 **WT `1/22/3 {seq}` → `22/3 {result,info,ruffianflag}`**（见
`2026-07-23-special-item-use-contract.md`）。Pixi `useItem` 此前把 829/830
落到通用 **`1/7/1`**；服务端专用 handler 不匹配该请求，响应里没有可解析的
`7/1`/`22/3` 使用结果，客户端 `feedbackFromBodies` 得到 `null` →「使用无响应」。

## 修改

- Pixi：`buildCombatPillUseRequest` / `useCombatPill` 走 `1/22/3`；成功后本地扣背包行（与 CBE 专用分支一致，响应无 `7/7`）。
- mock：成功 tip 改用 `vm_net_mock_special_item_success_info(829|830)`，避免神力丸仍显示「大力外功已生效」。

## 验证

1. 使用神力丸：提示含神力丸文案，背包数量 -1，点「修」可见「神力丸生效中(剩余…)」。
2. 已激活时再使用：同类效果拒绝文案，不扣物品。
3. 大力丸同路径。
