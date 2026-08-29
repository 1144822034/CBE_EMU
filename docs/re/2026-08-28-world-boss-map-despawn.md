# 世界BOSS：进战后地图未移除 / 刷新后旧怪残留

日期：2026-08-28

## 现象

1. 玩家与世界BOSS开战后，地图上的 BOSS 刷怪点仍在（本机与同图其他玩家）。
2. 新一轮世界BOSS 刷新后，上一只仍留在旧坐标/旧地图客户端视野里。
3. （续）开战成功后 BOSS 约 10s 又在地图刷回，像“不断刷新”。

## 预期

- Claim（首名开战）后：SCE kind=3 行从权威资源去掉，同图客户端地图标记消失且不再 10s 回刷。
- Despawn / 换点刷新：旧点移除；新点仅出现在新场景 SCE；停留在旧图的客户端不再画旧怪。

## 根因

1. `try_claim_encounter` 要求 **精确 home 坐标**；Pixi 挂机/战后用 `liveSceneMonsters` 上传 **巡逻脚印**，claim 失败 → SCE 未删 → 战后 `armSuppressedRespawns(10s)` 把怪刷回。
2. claim/despawn 虽广播 `[WBOSS:]`，但仅依赖 SCE 重拉；compose/WT18 若仍旧，同图他人仍看见 BOSS。

首次偏离：4/1 进战成功但 `world_boss_claim` 因 pos 不匹配未触发。

## 修改

- 服务端：claim 接受 home / 近距脚印 / wire·monsterId；删点后再按 actorId 清扫；`[WBOSS:claim]` 带 home pos。
- Pixi：`homeX/homeY` 用于 4/1；收到 claim/despawn 按 `pos=(x,y)` **永久**移除（不 10s 回刷）；再 forceReload SCE 并二次 drop。

## 验证

- [ ] 同图两人：A 开战，双方地图 BOSS 立即消失，10s/再碰不会回来。
- [ ] 挂机踩到 BOSS：服务端日志有 `world_boss_claim`。
- [ ] Admin 强制刷新到另一张图：旧图客户端旧怪消失。
- [ ] 普通野怪战后约 10s 仍回出生点。

## 续：普通野怪遇怪后不消失 / 战后立刻还在

### 根因

`onBattle` 把世界 BOSS 的 `markSuppressedPermanent` + `removeSceneMonsterPermanent` 套到了**所有** SCE 遇怪：

1. 碰撞路径已 `suppressSceneMonster`（临时隐藏）；
2. `onBattle` 再按 home 坐标永久删点，并清掉 `suppressedScene`；
3. `endBattleToWorld` → `refreshSceneCombatSpawns` 从 SCE 整表重载 → 无 suppress → 普通怪立刻刷回。

首次偏离：进战成功后地图标记本应临时隐藏并约 10s 回出生点，却因永久删点 + 战后刷新表现为“一直存在”。

### 修改

- `onBattle` 仅临时 `suppressSceneMonster`；永久移除只走 `[WBOSS:claim]` / despawn → `dropWorldBossFromMap`。

### 验证

- [ ] 踩普通野怪：进战地图标记消失；战后约 10s 回出生点（非整场立刻刷回）。
- [ ] 挂机遇怪：同样临时消失 + 10s 回刷。
- [ ] 世界 BOSS claim：双方地图仍永久消失。

## 续：Admin 二次强制刷 BOSS → `具名资源发布项已达到上限`

### 根因

`vm_net_mock_sce_combat_spawn_admin_save_all` 在写入 SCE 前对 **该图全部 kind=3 刷怪点** 逐个 `ensure_actor_resource_published`。户外图刷怪点多、Actor 依赖多时会快速占满 `VM_NET_MOCK_UPDATE_NAMED_MAX`（128）。另：batch publish 用精确文件名查 catalog，`.sce` 与无后缀 twin 被当成两条新项，加剧膨胀。

### 修改

- 运行时 inject/remove（世界 BOSS）：仅对 **本次坐标** 发布 BOSS Actor；despawn 跳过 Actor 发布。
- batch publish：用 `find_named_for_request` 识别 twin/后缀别名，只 bump 已有行。
- 上限 128→256；世界 BOSS 选图优先已在 update catalog 的户外图（**仅整点自动刷**；Admin 强制刷为纯随机）。

### 验证

- [ ] Admin 强制刷 → 开战 → 再强制刷：不再报 catalog 上限。
- [ ] 服务端日志若仍失败应出现 `named_resource_batch_publish_limit catalog_rows=… missing_rows=…`。
