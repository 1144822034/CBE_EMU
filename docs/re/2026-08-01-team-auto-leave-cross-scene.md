# 组队：队友不在队长场景时自动离队

> **2026-09-06 修订**：跨图策略已改为「队长切图队友跟随、队员禁自行切图」。  
> 本文仅保留 auto-leave 作为**安全网**说明；权威契约见  
> [`2026-09-06-team-follow-leader-scene.md`](./2026-09-06-team-follow-leader-scene.md)。

## 原需求（历史）

组队后若队友与队长不在同一场景，该队友自动离队，避免跨图幽灵队伍。

## 当前行为

- 离队名册变更仍走 `1/5/7 {id}`（`TEAM_LEAVE`）。
- 队长换图时：先 `team_queue_follow` 拉队友，**不再**因队长先落地而踢人。
- auto-leave 仍在 `vm_mock_service_session_mark_scene_ready`（场景名变化）触发，但跳过：
  - `pendingTeamFollowSceneEnter`
  - `sceneVisiblePending`（在途）
- 仍会踢：队员既无跟随在途、可见场景又与队长不一致的异常席位（例如断线重登落到错误图）。

## 触发

`vm_mock_service_session_mark_scene_ready` → `vm_mock_service_team_auto_leave_off_leader_scene`。

判定：`team_member_same_scene_presence`（`sceneVisible`，商城座可回落 `onlineScene`）。

## 验证（安全网）

1. 队员被强制不同图且无 follow 臂（异常路径）→ `team_auto_leave ... reason=scene-mismatch`。
2. 正常队长切图跟随路径 → 无 auto-leave，见 follow 文档。
