# 副本进图挂机走 challenge_enemy_id（2026-07-30）

## 症状

`29梦境空间_01` 等副本 SCE 为无战斗 Actor 的 stub；`automonster.dsh` 也无该行。后台「场景刷怪」只覆盖挂机三槽且**不**向地图注入实体。玩家进图后挂机得到 `action=no-monster`，地图上也点不到怪。

## 契约

副本向导动态 NPC（`npc_kind=6`）已有：

- `target_scene` / 落点：进入副本
- `challenge_enemy_id`：守关怪；菜单「挑战守关怪」走 `4/10` 非场景开战

进图后的挂机选怪此前**不**读 `challenge_enemy_id`，与「进图打怪应走挑战怪物 ID」不一致。

## 修改

1. 副本进入成功时，若向导配置了 `challenge_enemy_id`，按连接记住
   `instanceHangupScene` + `instanceHangupEnemyId`（日志
   `mock_instance_hangup_enemy_bind`）。
2. 挂机开战选怪：`automonster` / `server_scene_monsters` 未命中时，回退
   `vm_net_mock_select_instance_challenge_enemy_for_scene`：
   - 优先本连接进入绑定；
   - 否则任意启用的向导，其 `instanceScene` 等于当前场景；
   - 再否则当前场景内的仅挑战向导。
3. 开战仍走既有挂机 `4/10`（无 SCE live-node 时 `non-scene-subtype10`），不伪造地图怪。

菜单「挑战守关怪」路径未改。

## 配置（梦境空间）

在传送进 `29梦境空间_01` 的副本向导 NPC 上同时填写：

- 副本目标场景 = `29梦境空间_01`
- 挑战怪物 ID = 目录内有效 ID
- 落点（可 0,0 自动解析）

## 验证

1. `make -j2`
2. 重启服务；经向导进入梦境，日志含 `mock_instance_hangup_enemy_bind` 与
   `mock_npc_instance_enter ... challenge_enemy=...`
3. 场景内开挂机：`mock_hangup_battle_start ... table_scene=session:instance-guide-enter`（或 `dynamic-npc:instance-guide`），无 `no-monster`
4. 向导上「挑战守关怪」仍可开战
