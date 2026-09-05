# 跑环跳过 task.dsh 溯源怪（2026-09-01）

## 需求

击杀怪物、收集材料环：若怪物后台「出现位置／来源」不是可走进地图名，而是 `task.dsh#任务ID` 一类资源溯源，则不要分配给玩家。

## 根因

`vm_mock_world_boss_resolve_map_label_gbk` 在 DB/sMap 都解析失败时，会把资源叶名原样作为标签返回。  
因此 `firstScene=task.dsh#709` 会得到非空「地图名」`task.dsh#709`，旧版 `vm_mock_ring_monster_eligible_kill` 仅判断 `mapLabel[0]!=0`，任务专属怪（碧竹巨蛇、火凤凰等）仍进入击杀池；收集环则把其掉落一并入池。

## 修复

`mock_server_activities.c`：

- 新增 `vm_mock_ring_first_scene_is_playable_map`：拒绝含 `.dsh` 的溯源，且解析后标签仍含 `.dsh` 的也拒绝。
- 击杀池、收集掉落池共用该契约（世界 Boss 配置怪仍排除）。
- 描述用 `monster_scene` / `collect_drop_hint` 同样跳过非地图来源，避免旧环文案再露出 `task.dsh`。

## 验证

1. 接击杀环：描述括号内应为真实地图（如蜀山南门），不应出现 `task.dsh`。
2. 多次接环：不应抽到仅 task.dsh 溯源的怪（如火凤凰 / 东方不败，若无 SCE 地图位）。
3. 收集环：掉落提示为 `地图·怪物` 或雁门材料文案，而非 `task.dsh…掉落`。
4. 空池时该次仍回退拜访（既有逻辑）。
