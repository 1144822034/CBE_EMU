# 商店返回 kind2 30/1 插入战斗导致连续进战卡住

日期：2026-08-01

## 现象

买药/回图后连续点怪（或挂机）进战，客户端卡在进入战斗/结算交错状态。

## 证据（lxh001）

```text
shop_return_kind2_reenter_arm ... via=moveinfo-live
mock_challenge_battle_start ... enemies=2
shop_return_scene_enter ... via=scene-poll   ← 战斗中投递 30/1
map_stone_loading_clear ... remaining=3/2/1  ← 战斗中继续 30/2
mock_battle_start_blocked_by_settle ...
```

异步 flush 已正常（`async=1`），不是落库排队导致。

## 根因

1. scene-sync poll 把 `shop_return_scene_enter`（30/1）放在结算拆场之前。
2. kind2 无「战斗中 hold」门闩；moveinfo-live 武装后，开战仍可能被 poll 插入 EnterScene。
3. kind2 武装的 map-stone 30/2 亦在战斗中投递，与 Battle.cbm 抢生命周期。

## 修改

1. `shop_return_scene_enter`：armed / awaiting / exit pending 时 hold。
2. poll 顺序：结算 exit / post-exit / cooldown / hangup-delay **先于** kind2 30/1。
3. map-stone loading clear：战斗态 hold。
4. challenge/hangup 开战与 settle reenter：取消 pending kind2 + map-stone clear。
5. 异步 flush：`reflushWanted` 时不清脏（并 bump dirtySerial）。

## 验证

1. 商店买药回图 → 立即连续点怪：不应再出现战斗中 `shop_return_scene_enter`。
2. 可出现 `shop_return_scene_enter_hold ... reason=battle-active` 或
   `shop_return_kind2_reenter_cancel ... challenge-battle-start`。
3. 结算仍走 `settlement_exit_arm` → `settlement_exit phase=poll-delayed`。
