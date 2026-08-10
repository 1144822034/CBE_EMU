# Pixi 地图石传送无 NPC（传送点正常）

日期：2026-08-09

## 现象

- 世界/区域图用传送石进图后，地图可见但 `27/11` 目录 NPC 不显示。
- 同图经边缘/具名**传送点**走进则 NPC 正常。

## 契约对比

| 路径 | 进图 | 非空 `27/11` 归属 |
|------|------|------------------|
| 传送点 | `2/3` full-bootstrap 武装 `wait_post_enter` | 随后 `25/5+2/3+27/11+7/42` post-enter |
| 地图石（CBE） | deferred poll `30/1` → `2/3` current-scene（空门控 + `wait_wt6`） | 随后 `WT6/1`（len≈39 task-subset） |

## 根因

Pixi `requestMapTeleport` 在 deferred `30/1` 落点后误走**传送点** post-enter。

1. `30/1` 经 `append_scene_enter` 只 `mark_pending`，**不**武装 `wait_post_enter` / `wait_wt6`。
2. 落点 poll 可走 `startup-scene-sync-poll` 抢先消费一次性目录（或同场景 reuse 打到 `post-enter-repeat` 空门控）。
3. Pixi 对「含 `27/11` 的包（含空门控）」做**整表替换**；空 `27/11` 把已有/应有的 NPC 清掉。

传送点路径因 `wait_post_enter` 挡住 poll 抢目录，故表现正常。

## 修改

1. **mock deferred `30/1`**：武装 `wait_post_enter` + 清 per-client NPC ledger；poll 在 `direct_enter_pending` 期间 hold，避免抢目录。
2. **mock `once_or_empty`**：本场景仍 `pending` 时不因 ledger 直接空门控；`post-enter-repeat` 在未 seeded 或仍 `direct_enter_pending` 时 rearm。
3. **Pixi**：落点后优先 `post-enter(exitID=0)`；若仍空再 `2/3`+`6/1`。传送中若已有非空目录，忽略空 `27/11` 门控整表清空。

## 验证

1. **重启** `jh-online-server` 后硬刷新 Pixi。
2. 世界图→区域图→确认瞬移：服务端日志含 `mock_teleport_stone_deferred_enter`、`mock_scene_npc_poll_hold ... wait-post-enter`（或直接 `mock_scene_npc_seed phase=post-enter*`），画面有 NPC。
3. 传送点走进同目标图仍有 NPC。
4. `make -j2`；`cd PixiJS_web && npm run build`。
