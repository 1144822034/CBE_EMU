# 传送后进商城返回卡 loading（stale teleport hold）

日期：2026-08-01

## 现象

地图石/传送完成后可进商城；退出 mmShop 后进度条不消（卡死）。

用户 `bin/server_out.txt` 为 warn 过滤日志，未见 `shop_return_*` info；同路径历史证据见
`2026-07-31-shop-return-post-catalog-clear-hold.md` /
`2026-07-28-penglai-shop-return-after-teleport-stall.md`。

## 业务链路

```text
map-stone / teleport → (可能残留 direct/deferred/map pending)
  → 客户端已 sceneVisibleReady，可开 mmShop
  → 出店 shell 30/2 → poll 27/11 → post-catalog poll 30/2
  → build_pending_shop_return_loading_clear:
       liveTeleport = deferred || direct
       transferActive = sceneVisiblePending || liveTeleport
       post_catalog && transferActive → hold（不投 30/2）
  → 20s 后 shop_return_loading_clear_drop → 无 ResetDownloadState → 卡条
```

## 根因陈述

- **触发**：传送进图后立刻（或稍后）进出商城；会话已 `sceneVisibleReady`。
- **被违反的契约**：mmShop 开/关不是地图传送；出店 post-catalog `30/2` 只应在
  **真实进行中的** scene visibility pending / 活传送时 hold。
- **首个错误状态**：`direct_enter_pending` 或 `deferred_enter_valid`（或
  `last_scene_change_target_valid`）在图已 ready 后仍为 true →
  `post-catalog-transfer-active` 永久 hold。
- **证据**：2026-07-31 已去掉“仅 target_valid hold”，但仍把 stale
  `liveTeleport` 当活传送；clear 过期日志为 `shop_return_loading_clear_drop`。
- **排除**：本次落盘 `durable_role_write_invalidate` 不改 shop-return 包序；
  warn 里的 `update_chunk_missing` 是独立资源发布缺口，不是本 hold 的充要条件。

## 修改

1. `vm_mock_service_clear_stale_map_transfer_for_shop`：会话已 scene-ready 时清
   deferred/direct/map pending、`last_scene_change_target_valid`、地图石 poll clear。
2. 开店 `mark_shop_scene_npc_reseed_pending` 与出店
   `finish_shop_return_rehydrate` 调用上述清理。
3. post-catalog hold：若已 `sceneVisibleReady` 且 visible 场景=clear 场景，
   **忽略** stale `liveTeleport`。

## 验证

1. `make -j2`，重启服务。
2. 地图石传送 → 进商城 → 退出：
   - 可有 `stale_map_transfer_clear ... reason=shop-open|shop-return-rehydrate`
   - `mock_shop_return_loading_clear ... post_catalog=1` remaining 递减到 0
   - 无长期 `shop_return_loading_clear_hold ... post-catalog-transfer-active`
   - 进度条消失，可走
3. 未传送、普通进出商城回归仍正常。
