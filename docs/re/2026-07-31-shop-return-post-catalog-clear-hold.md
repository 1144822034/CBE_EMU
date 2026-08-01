# 地图石后出店：post-catalog 30/2 被 hold / 2s settle 截断（2026-07-31）

## 触发

地图石进 `c04临安府_05` → 开商城购买 → 退出。地图石 `same-reenter-allowed-once` 已通；出店后进度条不消。

## 证据链

```text
shop_return_loading_clear_settle ... post_catalog=0 via=2s-no-moveinfo
mock_shop_return_loading_clear ... remaining=0 post_catalog=0
shop_return_npc_catalog_ready ... via=shell-clear-done
mock_shop_return_npc_catalog_deliver ... prior_seeded=1 resp=105
shop_return_loading_clear_arm ... remaining=3 post_catalog=1 lite=1
# 客户端仅再见一次 poll resp=55，其后 poll 11/77（task prompt）
# 无 shop_return_kind2_skip / busy_ack / remaining→0 post_catalog=1
```

客户端：`ScreenInit` mmGame → type27 → shell clear → `27/11` → 一次 `30/2` → 任务提示轮询，进度条仍在。

## 根因

1. post-catalog poll `30/2` 在 `sceneVisiblePending || teleport || last_scene_change_target_valid` 时 **hold 且不检查超时**。NPC 出店已 skip kind-2，不应再被陈旧的 `last_scene_change_target_valid`（地图石 remember 残留）永久挡住。
2. hold 期间 poll 仍可走 `sceneVisibleReady` 门后的 task prompt（resp=77），表现为“有网无清条”。
3. 暖目录 `lite` 曾允许 post-catalog `2s-no-moveinfo` settle；过早 `remaining→1` + `busy_ack` 时，deferred `27/11` 的 DF DoLoading 可能尚未落定（`26/0` 不解 DF）。

与 wait_wt6 / same-reenter 地图石修复正交。

## 修改

1. [`mock_server_social.c`](../../src/server/mock_server_social.c) post-catalog hold：仅 `sceneVisiblePending` / 活传送；打 hold 日志；**不再**因单独 `last_scene_change_target_valid` hold。
2. 同文件：post-catalog **禁止** 2s settle（仅 shell `post_catalog=0` 可用）；lite 走满 `remaining=3`。
3. [`mock_server_equipment_npc.c`](../../src/server/mock_server_equipment_npc.c) `finish_shop_return_rehydrate`：无活传送时清 `last_scene_change_target_valid`。
4. 客户端 [`network-client.c`](../../src/network-client.c)：remote observation finish 时清 `allow_map_stone` 残留。

## 验证

1. `make -j2`，重启 mock + 客户端。
2. 临安府_05：地图石进入 → 商城进出（站立亦可）：
   - shell settle 可有 `post_catalog=0`
   - `catalog_deliver ... prior_seeded=1`
   - **多条** `mock_shop_return_loading_clear ... post_catalog=1` remaining 2→1→0
   - `shop_return_kind2_skip ... via=post-catalog-clear-done` + `busy_ack`
   - 无长期仅有 poll 77 而 clear 停在 remaining>0
3. 进度条消失，可走；NPC 仍在。
