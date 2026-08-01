# 地图石 wait_wt6：task-subset 漏 27/11 + poll 清条后再灌目录（2026-07-31）

## 触发

登录 `03丹霞山_01` 可走可动 → 地图石确认 → `01桃花岛_02.sce`（download=0）后进度条一直不消。

## 证据链

```text
mock_teleport_stone_current_scene_complete ... defer_npc=1
mock_scene_npc_seed_defer ... next=WT6/1
builtin-scene-task-subset-followup request=44 resp=215
  # 无 mock_scene_npc_seed_deliver / 无 fb11_ack
mock_map_stone_loading_clear ... remaining=2/1/0 reason=wait_wt6-timeout
mock_scene_npc_seed phase=startup-scene-sync-poll npcnum=2
  # loading_clear 已耗尽；非空 27/11 再开 DoLoading，无 ResetDownloadState
```

客户端：`same-suppressed` 后仅 scene_poll（含 resp=58 的 clear），随后 poll `resp=191`（NPC），进度条仍在。

## 根因

1. 地图石 download=0 在 `2/3` 置 `wait_wt6`，本应由 WT6/1 / type27 / **task-subset** 投递非空 `27/11` 并 `rearm` poll `30/2`。
2. 客户端实际发 `25/5+…+27/11`（len=44）走 `builtin-scene-task-subset-followup`，但 `27/11` 应答被关在 `primaryTaskSubsetNeedsFb11Ack` 分支内；该次请求未进入该分支 → **完全不回 27/11**，`wait_wt6` 不消费。
3. poll `map_stone_loading_clear` 因 `wait_wt6-timeout` 把 3 次 `30/2` 打完。
4. 之后 `startup-scene-sync-poll` 才下发 `npcnum=2` 的 `27/11`，再次挂起 DF loading，清条 arm 已空 → 卡死。

与户外 `clientmiss` 磁盘权威无关（资源均为 `uptodate`）。

## 修改

1. [`mock_server_interaction_login.c`](../../src/server/mock_server_interaction_login.c) task-subset：`wait_wt6` 且目录未播种时**无条件**投递 one-shot `27/11` 并 rearm clear；请求含 `27/11` 时否则回空门控。
2. [`mock_server_social.c`](../../src/server/mock_server_social.c) scene-poll：若本次消费的是 `wait_wt6` 目录，投递后 `arm`/`rearm` `map_stone_loading_clear`。

## 验证

1. `make -j2`，重启 mock。
2. 丹霞山 → 地图石 → 桃花岛_02：期望
   - `mock_scene_npc_seed_deliver ... phase=task-subset after=wait-wt6`，或
   - poll 路径 `arm_loading_clear=1` 后再见 `mock_map_stone_loading_clear`
3. 进度条消失，可移动/开背包；不应再出现 clear remaining=0 之后才首次 `npcnum>0` 且无后续 clear。

## 复测续记（同日）

task-subset 已投递 `27/11` 且 clear ×3 无 timeout，进度条仍卡。后续根因是 `same-suppressed` 拒掉 `27/12+posinfo` 第二次 EnterScene，见 [`2026-07-31-mapstone-same-reenter-loading-stall.md`](2026-07-31-mapstone-same-reenter-loading-stall.md)。
