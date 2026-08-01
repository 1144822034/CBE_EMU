# 地图石 download=0 进图加载卡住（无情谷）

```text
phase: map-stone direct teleport -> delayed 30/1 -> same-target 2/3 completion
trigger: 桃花岛经 WT16/4、WT16/2+WT16/3 到 17无情谷_01.sce（资源已齐 download=0）
request_shape: deferred WT30/1 {scene,posinfo}; WT2/3 {2/3,27/11,27/4,7/42}; optional later WT6/1
observed_target: 17无情谷_01.sce @ (167,48), download=0
client_parser: WT30/1 -> 0x010396D6; WT30/2 -> 0x01039770 -> ResetDownloadState 0x0103993C
```

## 运行时证据与首个偏差

```text
mock_teleport_stone_map_confirm ... scene=17无情谷_01.sce ... download=0
mock_teleport_stone_deferred_enter ... response=scene-channel-enter-confirm-target
mock_scene_change_teleport_resource_pending ... completion=defer-30/2-until-WT6/1 evidence=WT18/7-client-download
  （中间无 mock_update_chunk / WT18/7）
mock_teleport_resource_followup_complete ... completion=30/2-no-posinfo-after-WT18/7
scene_ready ... scene_sync_poll ... queue_age_ms=26100
```

`download=0` 且无真实资源下载时，通用 `2/3` 仍因 `teleport_stone_direct_enter_pending`
无条件 defer 无 `posinfo` 的 `30/2`。铜雀台曾用场景白名单特例修过；无情谷不在
`scene_uses_current_scene_completion` / 铜雀台特例内，于是再次踩坑。

成功契约（同 download=0 地图石，如蓬莱秘境）：

```text
2/3 -> 27/12 + 27/11 + 27/4 + 7/42 + 30/2(no posinfo)
```

## 根因

1. `deferTeleportResourceCompletion` 只匹配传送 pending，不看是否真需下载。
2. `current_scene_completion` 探测器仅允许固定场景名 + 铜雀台特例，未覆盖
   “任意 download=0 地图石直达”的通用契约。

## 修改

- `vm_net_mock_is_current_scene_completion_request`：凡 `teleport_stone_direct_enter_pending`
  且 pending 目标匹配、且 `!needsSceneDownload`，一律走 current-scene completion
  （同包末尾 `30/2` no-posinfo）。不再按场景名限制铜雀台。
- `deferTeleportResourceCompletion`：额外要求 `needsSceneDownload || !resourcesReady`，
  仅真实 WT18/7 下载期 defer。
- 铜雀台 NPC 延后到 WT6/1 的特例（`deferTongquetaiNpcSeedToFollowup`）保留。

## 验证

- `make -j2`
- 复测：桃花岛地图石 → 无情谷；期望日志
  `mock_teleport_stone_current_scene_complete ... 27-family+30/2-no-posinfo`，
  不应再出现无下载时的 `teleport_resource_pending`。
- 回归：仍需下载的地图石目标继续 `defer-30/2-until-WT6/1`，由真实 WT6/1 收尾。
- **2026-07-27：** `wait_wt6` 时 WT6/1 不得被
  `task-subset-followup-current-scene` 抢走，见
  `2026-07-27-teleport-wait-wt6-stolen-by-task-subset.md`。

## Follow-up 2026-07-25：临安府地图石 NPC 不显示

```text
mock_teleport_stone_current_scene_complete ... phase=current-scene-completion npcnum=3 once=1
mock_scene_resource_followup_repeat_ack ... completion=none
```

根因：铜雀台的「completion 回空 27/11，WT6/1 再下非空目录」只白名单了
`penglai01`；`c04临安府_01` 等同 download=0 地图石仍在 completion 消费目录，
壳未 runtime-ready → 节点丢失。

修改：凡 `closeTeleportDirectEnter` 一律 defer（`wait_wt6`）；WT6/1 按该标记投递；
poll 在窗口内 hold。期望
`mock_scene_npc_seed_defer ... next=WT6/1` 后
`mock_scene_npc_seed_deliver ... phase=WT6/1`。
