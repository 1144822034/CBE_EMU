# 服务 NPC 无选项 & 跨图仍能看见玩家

日期：2026-08-01

## 现象

1. 打开服务 NPC 对话后，没有功能（服务）选项，也没有任务选项。
2. 不在同一张图的玩家仍能在当前地图上看到对方。

## 根因

### 1. NPC 对话目录场景键与客户端地图壳不一致

`vm_net_mock_build_npc_dialog_response`（`26/1` type=1）用
`vm_net_mock_current_scene_name()`（`role->scene`）做
`collect_scene_npcinfo_seeds` 匹配。切图时 `remember_scene_change_target` 会先
`scene_pending` 并把角色场景写成目标图，而 `sceneVisibleScene` 在 pending 期间
仍指向**旧图**（客户端地图壳尚未换完时可点到旧图 NPC）。

```text
role->scene = 目标图
sceneVisibleScene = 旧图（pending）
点击旧图服务 NPC → catalog_match=0 → option-count=0
```

运行证据（`logs/server-battle.log`）：角色在 `01桃花岛_04.sce` 时
`mock_scene_npc_collect_empty ... match_enabled=0`；铜雀台动态 NPC 仅挂在
`c00蓬莱仙岛_01`。此前成功对话日志为：

```text
mock_npc_dialog ... scene=c00蓬莱仙岛_01 catalog_match=1 npc_kind=3 service_action≠0
```

功能/任务选项都依赖 `matchedSeed`；目录 miss 时两者同时消失。

### 2. scene-sync baseline 早退未下发 `1/2/6`

附近玩家由 `session_scene_is_visible` → `build_scene_role_seeds` 过滤，经
poll 投递 `1/2/10` / `1/2/2` / `1/2/6`。`needsBaseline` 为真时原逻辑只追加
`1/2/10` 后立刻 return，**不**扫描 `peerSync[]` 清除已离开本图的源。

```text
同图看到 A、B → A 切走 → 同 poll 需要给 C 做 baseline
→ 只发 C 的 1/2/10，A 的 peerSync.visible 仍为 1，无 1/2/6
→ 客户端继续渲染 A（跨图幽灵）
```

delta 路径本来有 remove 循环；baseline 路径遗漏是首个偏离。

## 修改

1. `mock_server_scene_sync.c`：`vm_net_mock_npc_dialog_scene_name()` 在
   `sceneVisibleReady || sceneVisiblePending` 时优先用 `sceneVisibleScene`；
   若该壳目录无命中再回退一次 `role->scene` 并打
   `mock_npc_dialog_scene_fallback`。
2. `mock_server_social.c`：抽出
   `vm_net_mock_append_stale_peer_remove6_objects`；baseline 与 delta 均在
   投递前/后清除不在当前 seeds 中的 peer，baseline 日志增加 `remove=`。

## 验证

1. `make -j2 server`
2. 双端同图 → 一人切图：留下者下一 poll 应见 `remove≥1` 的 baseline/delta，
   画面上对方消失；目标图在对方 `scene_ready` 后才出现 `1/2/10`。
3. 铜雀台点白展堂/郭靖：`mock_npc_dialog catalog_match=1`，有服务和/或任务选项；
   切图过程中点仍可见的 NPC 不应再稳定 `option-count=0`（可看 fallback 日志）。

## 仍未知

- `01桃花岛_04` 本身 `npcnum=0`（无动态行、SCE 无交互 NPC）属目录真相，不是本 bug。
- 若 `catalog_match=1` 且 `service_action≠0` 而 UI 仍空，需另查客户端
  `ParseNPCDialogData` / 描述长度，而非再改 seed 门闩。
