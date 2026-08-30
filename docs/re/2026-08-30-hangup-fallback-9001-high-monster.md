# 挂机低级图偶遇怪#9001

日期：2026-08-30

## 现象

野猪林南等低级图挂机时，偶尔进入战斗显示 `怪#9001`，属性明显高于本图野怪。

## 根因

Pixi 挂机是客户端 `4/1` 环（`startBattleNearPlayer` → `armEncounterVisualLikeCollide`）：

1. 战后 SCE 怪点 `suppress` 约 10s（`armSuppressedRespawns`），`liveSceneMonsters` / `session.sceneMonsters` 不含已开战点。
2. 下场挂机约 4s 触发；当图上刷怪点在抑制窗内被打光时，选怪池为空。
3. 空池硬编码 fallback `id=9001`；服务端按 catalog/wire 开战，公式对 `id≥200` 给约 45 级兽系属性 → 「厉害的怪」。

首次偏离：挂机在「本图无可用 live SCE」时仍发 `4/1 id=9001`，而不是继续用本图种子物种。

## 修改

- `GameSession.sceneMonsterCatalog`：进图/刷怪刷新时保存完整 SCE 种子；live 池被抑制时挂机回落种子物种与 home 坐标。
- 去掉野战 `9001` fallback；无怪可遇则拒战，挂机定时器再试。

## 验证

1. 野猪林南挂机连战：抑制窗内再遇怪仍为本图物种（非 `怪#9001`）。
2. 撞怪 / 点怪：物种与坐标仍跟 SCE 一致。
3. 城内 / 无刷怪图：不应用 9001 强开野战。
