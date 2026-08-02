# 地图石传送卡住：type27 用陈旧 role 场景消费 wait_wt6

日期：2026-08-01

## 现象

NPC 对话选项修复后，地图石跨图传送进度条卡住。日志（`lxh003` 临安府_10 → 桃花岛_01）：

```text
mock_teleport_stone_current_scene_complete scene=01桃花岛_01.sce ... defer_npc=1
map_stone_loading_clear_arm ... scene=01桃花岛_01.sce remaining=3
mock_scene_npc_seed_defer ... next=WT6/1
# 另一账号 poll 穿插后：
mock_scene_npc_catalog scene=c04临安府_10 ... phase=type27-map-stone-wait-wt6
scene_resource_followup_timing scene=c04临安府_10
map_stone_loading_clear_cancel ... armed_scene=01桃花岛_01.sce reason=scene-mismatch
# 客户端随后 2/3 回临安
```

## 根因

1. 地图石 `2/3` 完成后会话 `sceneVisible*=桃花岛`，并武装 `wait_wt6` +
   `map_stone_loading_clear(桃花岛)`。
2. 随后 type27 用 `vm_net_mock_current_scene_name()`（`role->scene`）。多账号
   restore / 快照可短暂把耐久角色场景拉回临安，而连接上的 `sceneVisible*` 仍是
   桃花岛。
3. `deliverWaitWt6Seed` **未**要求 `pending_scene == current`，把临安目录灌进
   `append_scene_npcs11_once_or_empty`，**消费**了目标图的 one-shot，并清
   `wait_wt6`。
4. poll 投递 loading clear 时用 `current_scene_name()` 与武装场景比对 →
   `scene-mismatch` 取消 `30/2` → 进度条永不关。

首个偏离：type27 在 `wait_wt6` 下用错误场景消费目录，而不是 `pending_scene`
   与会话壳不一致时的 hold。

## 修改

1. `vm_net_mock_client_visible_scene_name()`：优先 `sceneVisibleReady|Pending`
   时的 `sceneVisibleScene`。
2. type27：`scene` 取可见壳；`deliverWaitWt6Seed` 必须匹配 `pending_scene`；
   投递用 pending 目标；不匹配则空 `27/11` hold（不消费）。
3. map-stone loading clear / task-subset followup：比对/取景用可见壳。

## 验证

1. `make -j2 server`
2. 双端在线时，临安地图石 → 桃花岛：应见
   `type27-map-stone-wait-wt6 scene=01桃花岛_01`（或 `type27_hold wait-wt6` 后
   WT6/1 投递），`map_stone_loading_clear` 耗尽，无 `scene-mismatch` 取消。
3. 进图可走、可开背包；NPC 选项与跨图可见修复仍成立。
