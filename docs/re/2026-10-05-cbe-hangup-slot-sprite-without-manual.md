# CBE 未先遇怪时挂机立绘不是本图挂机槽

日期：2026-10-05

## 现象

CBE 野外直接点挂机（未先手动碰怪/点怪），战斗场景怪物形象不是当前地图挂机槽物种。先手动打一场再挂机则立绘正确。

## 契约

`mmBattle HandleBattleStartMsg(0x66CC)`：

- subtype-5：用 live 表 `index` + `node+240/+244` 拷贝场景节点的 id/名/HP/`.actor`。
- subtype-10：左侧 visual 只走玩家六槽 `GetMapTileData`；默认 `0/1` 是女天机，不能出怪立绘。

CBE 挂机上行是 `2/10 Type=2`，没有 Pixi `4/1` 的 `id/index/posx/posy`。独立服务（`CBE_SERVER_ONLY`）读不到客户端 `R9+0x5CB0`。

## 第一次偏离

未先 `4/1` 时没有 `session-live-node`。旧路径：

1. 精确匹配 live `actorId` 对不上挂机槽目录 ID / keep-id → `non-scene-subtype10` → 女天机。
2. 共享 keep-id `#250` 时命中表中第一个节点，立绘不是本次挂机槽 remap 物种。

先手动遇怪会把 `(wire,index,battlePos)` 记进 session，subtype-5 立绘正确。

## 修改

1. **CBE 客户端**（`network-client.c`）：确认挂机技能后 replay `2/10` 前，从 live 表取最近 kind=2 且 battlePos 非 0 的节点，写入 `id/index/posx/posy`。服务端走已有 `request-live-node` → `2/2+4/5`。
2. **服务端**：emulator live 扫描按 `remap(wire,pos)` 匹配挂机槽（禁止第一个 keep-id）；命中后写入 session，挂机环继续 subtype-5。

## 验证

1. 进有刷怪的野外图，不碰怪直接挂机：客户端 `cbe_hangup_live_bind`，服务端 `target_source=request-live-node subtype=5`，立绘为本图场景怪 / 挂机槽物种。
2. 先碰怪再挂机：`session-live-node` 或带 live 字段的 replay，立绘不错。
3. 城内无槽：仍拒挂机。
4. `make -j2`（含客户端，`network-client.c` 是 `main.c` include）。

## 仍未知

图上没有 kind=2 战斗节点时无法绑定，仍 subtype-10。后台挂机槽若与当前 live 刷怪完全不是同一物种，会显示最近 live 怪而非后台槽（无法凭目录 `actor_resource` 伪造场景 `.actor`）。
