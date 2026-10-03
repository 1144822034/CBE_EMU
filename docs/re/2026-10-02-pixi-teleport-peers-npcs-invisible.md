# Pixi 传送落点后附近玩家 / NPC 不可见

日期：2026-10-02

## 现象

传送点、地图石、配偶传送等到目标场景后，地图壳正常，但附近玩家和/或目录 NPC 不显示；打开「周围玩家」后玩家才出现。

## 根因

### 附近玩家

1. `onEnterScene` 无条件 `clearPeers()`，却未从 `mapPeersSnapshot()` 重绘。
2. 落点 poll 若已收到 `2/10` 并写入 `scenePeerById`，随后 `showScene` / `clearPeers` 清掉画面；服务端 baseline 已推过则不再重发 → 地图长期只有自己。
3. 普通传送 / 切图成功后未像切号、擂台那样主动拉 `2/7` 促进 `sceneVisibleReady` 并刷地图 peers。
4. 跨图时 `trackEnterScene` 清了 NPC，却未清 peer 缓存；若误重绘会把旧图玩家贴到新图。

### NPC

配偶传送等只靠 `30/1` / `32/x`、无 post-enter 时，跨图已清空的 `session.npcs` 不会被非空 `27/11` 填回（与地图石缺 settle 同类，见 `2026-08-09-pixi-map-teleport-missing-npcs.md`）。

梦境 `29*` 故意隐藏附近玩家，见 `2026-09-20-dream-hide-nearby-peers.md`。

## 修改

### Pixi

1. `trackEnterScene` 跨图：`dropScenePeers` 清缓存并通知 UI。
2. `onEnterScene`：非梦境在 `clearPeers` 后 `applyPeers(mapPeersSnapshot)` + 妆容；末尾 `await refreshNearbyPeersAfterEnter()`。
3. `refreshNearbyPeersAfterEnter`：每轮先 `pollOnce` 再 `2/7`；组队最多 8 次重试。
4. 传送 / 切图 / 跟随 / 配偶 / 多开切号均 await 该刷新。

### 服务端（2026-10-02 续）

`vm_mock_service_session_promote_nearby_after_land`：进图后 `mark_scene_ready` + 清 `peerSync` + HSP 同步。用于 post-enter、post-enter-repeat、team-follow 30/1、脱离卡死。

`capture_session_presence`：坐标暂不可用时不抹掉已有 presence（避免队友从 2/7 种子消失）。

### 续：组队双向不可见 / `no team peers 4临安府_10`

根因：`scene_names_equal_loose` 只去 `.sce`。脱离卡死/sMap 落点常为
`4临安府_10.sce`，队友登录可见键为 `c04临安府_10.sce` → 不相等 →
`build_scene_role_seeds` 漏掉队友 → `2/7 othernum=0`。

修复：城镇图 `c`/`c0` 数字前缀别名（保留中文段+槽位）；**不要**用
`数字_槽位` 指纹（曾把无关图判成同图导致切图走 completed-repeat）。
进图 promote 时顺带清队友观察端 `peerSync`。

## 验证

1. 两人组队同图（一账号曾脱离卡死落在 `4临安府_*`）：双方地图应互见。
2. 边缘传送点 / 地图石跨图仍应正常进图（非同场景空 ACK）。
3. 控制台不应再长期刷 `no team peers`。
4. `make -j2`；重启 `jh-online-server`；硬刷新 Pixi。
