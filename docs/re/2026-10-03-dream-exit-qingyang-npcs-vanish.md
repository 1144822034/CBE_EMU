# 2026-10-03：梦境出本庆阳宫 NPC 先显示再消失

phase: pixi-dream-exit-empty-27-11-wipe
status: fixed

## 症状

从梦境 `29*` 传送到户外（庆阳宫）：地图壳与目录 NPC 先出现，随后 NPC 全部消失且不再刷出。

## 契约

空 `27/11`（npcnum=0）是门控/ACK，不是「本图目录改为空」。跨图时 `trackEnterScene` 已把旧目录清掉；真正的庆阳宫目录来自随后的非空 `27/11`。

出本后 poll 必须还能投递 `map_stone_loading_clear` 的 `30/2`（再进梦境门闩），但不能用同路径里的空 `27/11` 覆盖已显示的目录。

## 第一次偏离

1. 出本 `30/1` → `trackEnterScene` 清空 `session.npcs` → `showScene`。
2. `ENTER_INSTANCE` / 传送点 post-enter 领到非空 `27/11`，NPC 画出。
3. 服务端 per-client ledger 已 seeded；其后 heartbeat、`refreshNearbyPeersAfterEnter` 的 poll/`2/7` 走 `once_or_empty` → **空 `27/11`**。
4. Pixi 原先只在 `sceneTransferExpected` 期间忽略空门控。使者出本不设该锁；传送点路径在 `finally` 清锁后同样会吃到空包。
5. `onNpcs([])` 整表替换，庆阳宫 NPC 消失；没有第三次非空目录。

## 修改

| 位置 | 改动 |
|------|------|
| `GameSession.handleBodies` | 当前已有非空目录时忽略空 `27/11`（不限传送锁） |
| `pickNpcOption` ENTER_INSTANCE | 改走 `settleLandedSceneCatalog`（post-enter 仍空再 `2/3`+`6/1`） |

## 续：出本后延迟整图刷新（2026-10-03）

刚出本画面已正确，过一会儿人物落点与 NPC 再闪一次。

原因：出本 `30/1` 已带目录并完成 `showScene` 后，仍发 post-enter / 二次 `30/1`，同图再 `trackEnterScene` 并把相同 `27/11` 整表重建。

| 位置 | 改动 |
|------|------|
| `settleLandedSceneCatalog` / `requestSceneChange` | 已有非空目录则不再发 post-enter |
| `handleBodies` 30/1 | 当前图已显示则不重挂壳；已有落点则不改脚位 |
| `handleBodies` 27/11 | 目录 id/坐标/名不变则不 `onNpcs` |
| `ENTER_INSTANCE` | 不再二次 `refreshNearbyPeersAfterEnter`（进图回调已拉过） |
