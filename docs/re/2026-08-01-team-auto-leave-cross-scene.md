# 组队：队友不在队长场景时自动离队

## 需求

组队后若队友与队长不在同一场景（换图 / 传送石 / 跨图重登），该队友自动离队，避免跨图幽灵队伍与遇怪/收编不一致。

## 契约

- 离队名册变更仍走 `1/5/7 {id}`（`TEAM_LEAVE` / 主动离队同路径）。
- 主动 `1/5/6` 离队：请求响应带 `5/7`；自动离队无请求，须给**离队者本人**也入队 `TEAM_LEAVE`。
- 队长不因换图被踢；只移除非队长且不在队长场景的席位。

## 触发

`vm_mock_service_session_mark_scene_ready`：本连接可见场景名变化（含首次建立）后调用
`vm_mock_service_team_auto_leave_off_leader_scene`。

判定与组队开战收编一致：`team_member_same_scene_presence`（`sceneVisible`，商城座可回落 `onlineScene`）。传送中（无 presence scene）不踢，等 scene-ready。

## 修改

- `src/server/mock_server_equipment_npc.c`：`team_auto_leave_off_leader_scene` + scene-ready 挂钩

## 验证

1. 双人同图组队；队员传送石去另一图 → 双方收到 `5/7`，队员 HUD 离队；日志 `team_auto_leave ... reason=scene-mismatch`。
2. 队长换图、队员留原图 → 队员被踢离队；队长仍在队（单人席）。
3. 同图进商城（visible 仍挂队伍图）→ 不离队。
4. `make -j2 server`。
