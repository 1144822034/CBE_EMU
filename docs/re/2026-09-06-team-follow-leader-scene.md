# 组队：队员禁切图 / 队长切图队友跟随

日期：2026-09-06

## 需求

1. 多人组队时，非队长不得自行切图 / 传送石 / 任务传送 / 配偶传送。
2. 队长切图不离队；队友自动跟随到队长落点，队伍保持。

## 旧契约（已废止）

`2026-08-01-team-auto-leave-cross-scene.md`：场景名不一致则踢非队长。队长换图会把留在原图的队员踢掉。

## 新契约

- **队员发起传送**：服务端拒绝（portal `2/3` 回 `25/12+1/3/3`「队员不能切图。」；传送石 / 任务传送回空 WT + 系统提示「队员不能传送。」；配偶 `32/2` tip 同文案）。Pixi 客户端同步软拦。
- **队长传送**：`session_mark_scene_pending` 时对不在目标图的队员 `team_queue_follow`：标 `sceneVisiblePending` + `pendingTeamFollowSceneEnter`，提示「跟随队长切图」。
- **跟随落地**：队员 poll / WT dispatch 优先投递 `30/1`（与传送石 deferred enter 同形），再走常规 post-enter / scene-ready。
- **auto-leave**：仍作安全网；跳过 `pendingTeamFollowSceneEnter` 或 `sceneVisiblePending` 的席位，避免队长先落地把在途队友踢掉。

## 修改点

- `mock_server_equipment_npc.c`：session 跟随字段、transfer_blocked、queue/clear follow、auto-leave 放行在途
- `mock_server_social.c`：portal 拒绝；poll 投递 follow enter
- `mock_server_scene_task.c` / `scene_sync.c` / `marriage.c`：传送族拒绝
- `mock_server_dispatch.c`：WT 路径可投递 follow enter
- `PixiJS_web`：`isTeamMemberNotLeader` + portal / 世界地图 / 配偶软拦；poll 检测到跨图后补 post-enter 拉 NPC（`maybeSettleTeamFollowEnter`）

## 回归（2026-09-06 晚）

现象：队长切图后 tip「成员离开 928」，跟随未生效。

根因：
1. `team_queue_follow` 在目标 `x=y=0`（portal pending 常见）时直接 return，未武装跟随。
2. 队长 `scene_ready` 仍走旧 auto-leave，把留在原图的队员 `5/7` 踢掉。

修复：
1. 零坐标回落 SCE spawn / 队长落点 / (120,120)。
2. auto-leave：若触发席是队长 → 只 `team_queue_follow`，不踢人。
