# 队友出商城后仍「未准备好」无法进战

日期：2026-08-01

## 症状

队长遇怪 →「队友未准备好」→ 队友退出商城回到地图（可走动）→ 队长再点怪仍被拒。

## 根因

`session_team_battle_not_ready_reason` 把出店后的 housekeeping 也当成未准备：

- `shopReturnLoadingClearPending`（poll 30/2）
- `shopReturnBusyAckPending`
- `shopReturnNpcCatalogPending`
- `pendingShopReturnSceneEnter`（空 NPC 图 kind2 30/1）
- `mapStoneLoadingClearPending`

`finish_shop_return_rehydrate` 已清 `mmShopShellActive` 并 `scene_ready`，却又武装
loading-clear /（空图）kind2。队友已在 mmGame 行走时，这些标志仍挡 `4/1`。

空 NPC 场景（如 `01桃花岛_01`）尤其明显：走动触发 `kind2-pending`，kind2 未投完前
整队无法开战。

## 契约修正

未准备好 = **商城壳仍占屏** 或 **场景未 ready**。  
出店后的 30/2 / 27/11 / kind2 / busy_ack 是 mmGame 收尾，不挡组队开战。

## 修改

`vm_mock_service_session_team_battle_not_ready_reason` 只保留：

1. `mmShopShellActive`
2. `shopSceneNpcReseedPending` 且场景仍未 ready
3. `!sceneVisibleReady || sceneVisiblePending`

## 验证

1. 队友进商城 → 队长点怪 → `reject-party-not-ready reason=mmShop-shell`。
2. 队友出商城（可走动）→ 队长点怪 → `team_battle_queue` / `mock_team_battle_start`，
   不再因 `shop-return-kind2-pending` / `shop-return-loading-clear` 拒绝。
3. 仍在 mmShop 时不得进战。
