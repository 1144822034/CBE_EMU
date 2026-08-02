# 登录落在地图左上角无法移动

## 证据（debug-8a65aa）

1. MySQL 角色 `158`：`c04临安府_05` **pos=(9,29)**
2. `normalize` 未改写（旧逻辑只修 `x==0||y==0`）
3. `mark_scene_ready` / `scene-startup-followup-complete` 均使用 `(9,29)`
4. 无 moveinfo 上传；portal safe-landing 未作用于该点

## 根因

持久化坐标落在地图近原点边缘（`< SCENE_LANDING_SAFE_GAP=32`），客户端进图后角色在不可走区域，表现为冲到左上角且动不了。

## 修复

- `normalize` / 存盘 / startup followup：对边缘坐标做 SCE 合理落点救援并 dirty 回写
- startup followup **不再**用宿主 `read_current_player_grid`（Global_R9）覆盖远端角色坐标
