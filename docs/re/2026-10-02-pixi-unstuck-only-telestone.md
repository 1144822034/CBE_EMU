# Pixi 脱离卡死后只剩传送石

日期：2026-10-02

## 现象

地图石/传送点进图正常；菜单「脱离卡死」后地图壳与 SCE `n_telestone` 可见，目录 NPC 与附近玩家不显示。

## 契约

| 步骤 | 包 |
|------|-----|
| 菜单确认 | `0/12/3 {id}` |
| 服务应答 | `12/3` + `16/3 {result=2, scene, posinfo, exitid}`（direct enter） |
| CBE 跟进 | `1/16/3 {exitID=当前X, type=0}` + 空 `27/11` + 空 `7/42` |
| 服务应答 | 非空 `27/11` + `7/42`（`builtin-scene-runtime-direct-enter-object-stream`） |

证据：`docs/re/2026-07-23-unstuck-scene-authority.md`、`JianghuOL.CBE:scene_runtime_init_and_sync`。

## 根因

Pixi `requestUnstuck` 只处理 `12/3+16/3` 落点并 `trackEnterScene` 重载壳：

1. 跨图（或场景 key 漂移）时清空 `session.npcs`；
2. `showScene` 从 SCE 重挂传送石；
3. **从不**发 runtime `16/3+27/11+7/42`，目录 NPC 回不来；
4. 误用传送点 post-enter 也不行：`mark_direct_scene_enter_completed` 后只回空 `27/11` 重复 ACK。

## 修改

1. `buildSceneRuntimeDirectEnterFollowupRequest(posX)`（`protocolExtras.ts`）。
2. `requestUnstuck`：落点 → `awaitEnterScene` → 发上述跟进 → `await refreshNearbyPeersAfterEnter`。

### 续：组队队友地图不显示 / 周围玩家有时为空（同日）

壳重载后 `peerSync[].visible` 仍为已推送 → scene-sync 不再发 `2/10`；客户端
`clearPeers` 后地图空。过早 `2/7` 在 mates 仍 pending 时 `othernum=0`。

服务端：`settings-unstuck` 落点后 `mark_scene_ready` + `invalidate_nearby_observer_cursors`
（同 mmShop/地图石 complete 契约）。客户端：组队时 `2/7` 空则 `pollOnce` 重试最多 3 次；
跟随队长 settle 也 await 该刷新。

## 验证

1. 任意有 NPC 的场景卡住 →「脱离卡死」→ 落点附近应有目录 NPC，同图其他玩家可见。
2. 两人组队：队长脱离卡死 / 队员跟随切图后，地图应直接看到队友，无需先开「周围玩家」。
3. 服务日志含 `nearby_observer_cursors_invalidate ... reason=settings-unstuck` 与
   `mock_scene_runtime_direct_enter_object_stream`；`27/11` 非空。
4. 地图石传送仍正常；勿出现控制台 `unstuck landed without 27/11 NPCs`。
5. `cd PixiJS_web && npx tsc --noEmit`；`make -j2`。
