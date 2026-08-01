# 锁妖塔边缘传送门加载卡住（_07 → _08）

Date: 2026-07-25

Status: implemented (server)

```text
phase: local SCE edge portal -> WT2/3 -> mmgame 25/5 followup
trigger: 16锁妖塔_07.sce @ (206,356) -> 16锁妖塔_08.sce
request: WT2/3 len=87 (after 2/10 actor-other); WT25/5 len=44
handler_before: builtin-scene-change (ack-only) + builtin-mmgame-scene-transfer-followup
client: ScreenInit caller=01018150 then 010182a6 depth=2; then resp=151/426/23; no further traffic
```

## 运行时证据与首个偏离

服务端（卡住）：

```text
mock_scene_portal_exit_mismatch ... request_exit=1 portal_entry=1 targetEntry=3 match=trigger-rect
mock_scene_change_source_portal ... target=(48,37)
scene_pending ... scene=16锁妖塔_08.sce pos=(48,37)
builtin-scene-change request=87 response=151
mmgame-transfer-followup ... needsDownload=0 ... valid=1
mock_mmgame_scene_transfer_followup ... objects=10 resp=426
builtin-scene-default-event resp=23
```

客户端：

```text
dp_change ... caller=01018150 ... ScreenInit Ok
... moveinfo / actor-other ...
dp_change ... caller=010182a6 ... ScreenInit Ok
queue_data ... resp=151
queue_data ... resp=426
queue_data ... resp=23
# 之后无网络/无出图
```

首个协议偏离：跨图 `WT2/3 len=87` 走了**通用 ack-only**（`30/2` 无 `posinfo` + pending），
再靠 mmgame followup 的 `30/2(no-posinfo)` 收尾。该形状只适用于「目的地壳已
runtime-ready」的蓬莱反向路径；锁妖塔本地壳仍停在 loading（`FindEmptyActorSlot`
`0x010182A6` / `EnterSceneByMapName` `0x01018150`），需要**带坐标的一次**
`30/2` 才能离开加载（见 `docs/re/2026-07-16-scene-transfer-single-load.md`）。

## 排除项

- `mock_scene_portal_exit_mismatch`：`request_exit=1` 等于源 `portal_entry`，不等于
  `targetEntry=3`。落点仍按源 `entry_id` 查目标 spawn `(48,37)`，与既有 SCE 契约一致；
  警告本身不是卡住根因。
- 空 NPC / 漏 `27/11`：本日志已有 `npcnum=0 immediate=1` 与
  `source=27/11-catalog`（`docs/re/2026-07-25-post-battle-empty-npc-transfer-stuck.md` 已修）。
- 资源下载：`needsDownload=0`，无 `WT18/7` 死循环（`_01` 地图名空格问题不涉及 `_08`）。
- 付费 `2/9`：本链是边缘 `2/3`，不是命名付费阵。

## 根因

`vm_net_mock_should_use_full_scene_bootstrap()` 只白名单蓬莱 / 桃花岛特例。
`16锁妖塔_*` 等非蓬莱 SCE 边缘传送落入 generic split：

1. `2/3` → `30/2(no-posinfo)` + remember pending（`resp=151`）
2. `25/5` → mmgame followup → 再一次 `30/2(no-posinfo)`（`resp=426`）

客户端 loading 从未收到 position-bearing completion → 进度条/加载壳卡住。

同形历史证据：`2/10 + 2/3 len=87` 用 generic ack-only 会半进图失败
（`docs/re/2026-06-24-scene-transition-loading-baseline.md` §12）。

## 修改

`should_use_full_scene_bootstrap`：对**非蓬莱**、跨场景、且已解析出 SCE entry spawn
（`hasSceEntry` 且坐标非 0）的目标，走与蓬莱前进相同的 full-bootstrap：
资源对象 + **唯一**带 `posinfo` 的 `30/2`，完成并清 pending。

蓬莱 transfer 场景仍走原 forward/reverse 分流，避免回归
`00蓬莱仙岛_02 -> c00蓬莱仙岛_01` 的 mmgame ack 路径。

## 验证

- [x] `make -j2`
- [ ] 复测：`16锁妖塔_07` 边缘门进入 `_08`；期望
  `mock_scene_npc_rearm ... trigger=scene-change-full-bootstrap`，
  `2/3` 响应明显大于 ack-only 的 151，且随后可移动/交互
- [ ] 不应再出现同一次传送后仅 `mmgame-scene-transfer-followup` + `resp=23` 后静默
- [ ] 回归：蓬莱 `_01↔_02`、地图石 download=0、付费 `2/9` 进图

## Follow-up 2026-07-25：蓬莱 `_02 → c00_01` 同形卡住

锁妖塔修复后复测：

```text
00蓬莱仙岛_02 @ (128,53) -> c00蓬莱仙岛_01 @ (223,370) exit=1
WT2/3 len=90 -> builtin-scene-change resp=150
WT25/5 len=44 -> mmgame-scene-transfer-followup resp=722
WT25/5 len=9  -> scene-default-event resp=23
```

根因：full-bootstrap 的蓬莱互跨条件带了 `exitId == 0`，本路径 `exit=1` 仍落
ack+mmgame。与锁妖塔相同，本地 loading 壳未 ready，无坐标 `30/2` 无法出图。

## Follow-up 2026-07-25：蓬莱进图后 NPC 不显示

加载已通后日志：

```text
mock_scene_npc_seed phase=startup-scene-sync-poll ... npcnum=3 once=1
mock_scene_change_post_enter_repeat_ack ... objects=4 resp=114
# 无 phase=post-enter-repeat 的非空 27/11
```

根因：full-bootstrap 已 `immediate=0 next=post-enter`，但 scene-sync poll 抢先消费
one-shot；post-enter 只拿到空目录。poll 不是新壳的 `27/11` 契约层。

修改：full-bootstrap arm 时置 `wait_post_enter`；poll 在 completed-scene 窗口内
hold，留给 `post-enter-repeat` 下发。期望日志
`mock_scene_npc_poll_hold ... wait-post-enter` 后
`mock_scene_npc_seed phase=post-enter-repeat npcnum=3`。
