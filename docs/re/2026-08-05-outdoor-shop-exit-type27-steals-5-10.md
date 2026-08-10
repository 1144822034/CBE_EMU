# 野外退商城闪退：type27 抢 5/10 跳过 rehydrate

日期：2026-08-05

Status: implemented (server) — outdoor only path; dream type27 仍为独立 27/4/25/5

## 现象

野外（例：`01桃花岛_01.sce`，type-21 npcnum=0）开商城再退出 → 客户端闪退。
梦境退商城已正常，本修不改梦境契约。

## 日志证据（`lxh001` / `46217406`）

```text
shop_return_resources_warm_capture … scene=01桃花岛_01.sce warm=0
scene_npc_reseed_arm … trigger=shop-open
mock_shop_open14 …
# 无 shop_return_rehydrate
mock_shop_return_type27_gate … warm_npc=0
  evidence=empty-27/11-WT6/1-owns-27/11+poll-30/2
net_send wt=5/10 len=40 source=builtin-type27-followup resp=92
session_reuse … end=next-frame-fail
```

对照正常梦境退出：

```text
shop_return_rehydrate …
net_send wt=5/10 … builtin-group-type1 resp=2740
… 再独立 type27 / early-shell …
```

## 根因

1. **触发**：mmShop→mmGame 首包常为 `5/10`（+`7/7` type1），同请求内还嵌
   `27/4`。
2. **违反契约**：`vm_net_mock_build_response` 在 group-type1 **之前**匹配
   `contains 27/4` → type27 认领该包。`shopSceneNpcReseedPending` 仍真 →
   empty `27/11`+fb4（resp≈92），**不**走 `finish_shop_return_rehydrate`。
3. **首个错误状态**：客户端期望背包壳 / rehydrate 契约，却收到空 type27 门闸。
4. **证据**：`wt=5/10` + `source=builtin-type27-followup` + 无 `shop_return_rehydrate`
   + `next-frame-fail`。

已排除：梦境 early-shell / kind2_skip（本会话梦境路径完整且可玩）。

## 修改

`vm_net_mock_build_type27_followup_combo_response`：若请求含 `5/10` 则 `return 0`，
让后续 `builtin-group-type1` 完成 rehydrate。独立 type27（`27/4` / `25/5`）不变。

## 验证

1. 野外空 NPC 图（桃花岛）开商城退出：日志应有
   `mock_type27_defer_to_group_type1` → `shop_return_rehydrate` →
   `builtin-group-type1`（大 resp）→ loading clear →（空图）`kind2_reenter`；
   无 `wt=5/10 … type27-followup resp=92`；不闪退。
2. 梦境退商城：仍走 rehydrate + early-shell / kind2_skip，无二次加载回归。
