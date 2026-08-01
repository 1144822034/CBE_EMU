# 地图石 same-suppressed：27/12+posinfo 被拒导致进度条半开（2026-07-31）

## 触发

桃花岛_02 → 地图石 → 丹霞山_01（资源 `uptodate`）。wait_wt6 / task-subset 已投递 `27/11`，poll `30/2` clear ×3 也已下发，进度条仍不消。

## 证据链

```text
remote_scene_target_apply ... subtype=1 scene=03丹霞山_01.sce
# deferred 30/1 → EnterSceneByMapName（第一次）
mock_teleport_stone_current_scene_complete ... 27/12+posinfo ... defer_npc=1
queue_data resp=219
remote_scene_target_complete_pending ... evidence=WT30/2
screen_mgr same-suppressed ... scene=03丹霞山_01.sce
# 第二次 EnterScene（绑定行走精灵）被拒；busy 标志未清
mock_map_stone_loading_clear ×3
# 进度条仍在
```

## 根因

1. 防梦境二次 ScreenInit 崩溃（`pc→0x4ad5542`）引入 remote observation + `same-suppressed`：同 serial 同场景禁止第二次 `EnterSceneByMapName`。
2. 户外地图石契约仍是：deferred `30/1` 先进一次壳，随后 `2/3` 的 `27/12+posinfo` 再进一次以绑定行走精灵。
3. `same-suppressed` 发生在 guest 已部分 arm download busy（`r9+21808/21804`）之后；拒绝换屏却不清 busy → 进度条半开。poll `30/2` / `ResetDownloadState` 路径不足以补上这次被拒的完成态。
4. wait_wt6 task-subset 漏 `27/11` 是相邻问题（见 `2026-07-31-mapstone-wait-wt6-task-subset-loading-stall.md`），修完后本路径仍卡，故独立根因。

## 修改

1. 客户端 [`src/network-client.c`](../../src/network-client.c)：remote `30/1` 且目标非 `29*` 时 arm `g_vm_scene_allow_one_map_stone_completion_reenter`。
2. 客户端 [`src/main.c`](../../src/main.c)：`same-reenter` 命中且 allow 置位 → 放行一次（`same-reenter-allowed-once`）；否则 suppress 并 `vm_host_clear_download_busy`（对齐 `ResetDownloadState:0x0103993C`）。
3. 服务端 [`mock_server_social.c`](../../src/server/mock_server_social.c)：`29*` 地图石完成仍用 `27/12-ack`（不发 posinfo、不诱发二次 EnterScene）；户外保持 `27/12+posinfo`。
4. Android JNI 同步：`JianghuOL/.../cbeEmu/network-client.c`、`main.c`。

## 验证

1. `make -j2`，重启 mock + 客户端。
2. 桃花岛_02 → 地图石 → 丹霞山_01：期望
   - `allow_map_stone_reenter=1`
   - `screen_mgr same-reenter-allowed-once`
   - 进度条消失，角色可见可走
3. 梦境 `29*`：仍见 `27/12-ack`，无二次 ScreenInit 崩溃；`allow_map_stone_reenter=0`。
