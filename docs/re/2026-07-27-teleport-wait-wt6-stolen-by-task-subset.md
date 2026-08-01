# 地图石后 WT6/1 被 task-subset 抢走导致 wait-wt6 / UI 卡住

Date: 2026-07-27

Status: implemented (server)

```text
phase: map-stone download=0 -> 2/3 current-scene-complete (wait_wt6) -> WT6/1
trigger: 临安府地图石 -> 01桃花岛_01；随后打开背包卡住
server_before:
  current_scene_complete ... seed_defer next=WT6/1
  type27_hold wait-wt6
  builtin-scene-task-subset-followup-current-scene resp=184   ← 错路
  mock_scene_npc_poll_hold wait-wt6 (重复)
  最终 startup-scene-sync-poll 空 27/11（90 tick 窗口过期）
missing: mock_scene_npc_seed_deliver ... WT6/1 ... completion=30/2-no-posinfo
```

## 根因

`is_current_scene_task_subset_followup_request` 对 len=39 且「刚完成的同场景」
为真，dispatch 优先走轻量 task-subset，**不会**执行
`tongquetaiNpcSeedAfterCurrentCompletion`（resource-followup）上的：

- 非空/空 `27/11` 一次投递
- 收尾无坐标 `30/2`（清二次 `EnterSceneByMapName` loading）

于是 `wait_wt6` 长期 hold；客户端 loading/背包交互卡住。与商城
`shop_return_loading_clear` 无关（本日志无该行）。

## 修改

`wait_wt6` 且 pending 仍指向当前场景时，
`is_current_scene_task_subset_followup_request` 返回 false，让同一 WT6/1
落入 `builtin-scene-resource-followup`。

## 验证

1. `make -j2`；重启服务
2. 地图石进 `01桃花岛_01`：期望
   `builtin-scene-resource-followup` +
   `mock_scene_npc_seed_deliver ... completion=30/2-no-posinfo`
3. 不应出现：`task-subset-followup-current-scene` 紧接在 `seed_defer` 之后且
   长期 `poll_hold wait-wt6`
4. 进图后可开背包；商城退出与传送互不抢包

## 回归（2026-07-31）

len=44 task-subset 在 `primaryTaskSubsetNeedsFb11Ack` 为假时仍可能漏 `27/11`，
导致 `wait_wt6-timeout` 先打完 clear、再 poll 灌非空目录卡进度条。见
[`2026-07-31-mapstone-wait-wt6-task-subset-loading-stall.md`](2026-07-31-mapstone-wait-wt6-task-subset-loading-stall.md)。
