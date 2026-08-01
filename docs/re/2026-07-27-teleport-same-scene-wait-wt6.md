# 同场景地图石：wait_wt6 永不收到 WT6/1（人物不显示再卡 loading）

Date: 2026-07-27

Status: implemented (server)

```text
phase: already on c04临安府_05 -> map-stone same scene -> deferred 30/1 -> 2/3 wait_wt6
trigger: 登录已在临安府_05，再用地图石选临安府落点仍解析到 _05
server: seed_defer wait_wt6; early map_stone_loading_clear age=7;
        type27_hold; task-subset resp=184; endless poll_hold wait-wt6;
        无 builtin-scene-resource-followup / WT6/1
client: 场景先出来、人物不显示，随后又 loading 卡住；仍有 moveinfo
```

## 首个偏离

1. `confirmed_exit` 最终场景与当前场景相同（`same_scene=1`），但仍
   `defer_npc=1` / `wait_wt6`
2. 同场景壳不会发 WT6/1；type27 空 hold + task-subset 空 `27/11` ack 让
   `wait_wt6` 长期 `poll_hold`
3. poll 延迟 `30/2` 在 type27 之前就投递（`age_ticks=7`），异步 ScreenInit
   再挂 loading 后无收尾

## 根因

`wait_wt6` 假定后续必有 WT6/1。同场景地图石违背该假定。type27 为保 WT6/1
而空 hold，反而饿死 NPC/人物节点与二次 loading 清理。

## 修改

1. 确认传送时记录 `teleport_stone_same_scene`；同场景则 `2/3` **不**
   `wait_wt6`，当场投递 `27/11`
2. 若仍 `wait_wt6` 且先到 type27：改为投递 catalog + 末尾 `30/2`（不再空 hold）
3. 若仍 `wait_wt6` 且先到 task-subset len=44：同样投递 catalog + `30/2`

## 验证

1. `make -j2`；重启服务
2. 已在临安府_05 时再用地图石进 `_05`：期望
   `same_scene=1`、`defer_npc=0`、`mock_scene_npc_seed phase=current-scene-completion`
   （或 type27/task-subset 的 `seed_deliver`），**无**长期 `poll_hold wait-wt6`
3. 人物与 NPC 可见，loading 消失；跨场景地图石仍可 defer 到 WT6/1/type27
