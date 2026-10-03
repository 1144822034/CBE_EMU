# 副本死亡回原图后再开战，消失的怪不刷新

日期: 2026-10-03

## 触发

1. 副本地图开战（SCE 点被 suppress，`respawnAt=null`）。
2. 死亡普通复活回到原图 / 同壳传送石（`20/1+30/1`，不走 `endBattleToWorld`）。
3. 再踩怪进战；先前消失的刷怪点一直不回来。

## 预期 / 实际

- 预期：战后约 8s 回出生点；同壳复活也应把死亡那场的 suppress 武装起来。
- 实际：种子从表里丢掉，`respawnSceneMonster` 因 `sceMonsters` 无此 id 直接 return。

## 契约 / 首次偏离

碰撞 / 点击会 `session.sceneMonsters = seedSceneMonsters()`，**不含**已 suppress 的点。

`onEnterScene` 同壳（副本地图往往带着 `n_telestone`，BFS hops=0）仍：

```text
view.actors.setSceneMonsters(session.sceneMonsters)  // 残缺 live 表
```

`ActorLayer.setSceneMonsters` 按 home 重绑 suppress：新表里没有该点 → **丢掉 suppress**，且新 `sceMonsters` 也没有该种子。死亡离场又不 `armSuppressedRespawns`。再进战 `endBattleToWorld` 的 SCE 刷新若空列表还会把图上的怪整表清掉。

## 修改

| 位置 | 改动 |
|------|------|
| `onEnterScene` 画怪 | 优先 `sceneMonsterCatalog`（完整 SCE），不用 live 过滤表 |
| 死亡拆战斗 | 补 `armSuppressedRespawns(8s)`（原先只有 `endBattleToWorld` 会 arm） |

## 验证

1. 副本踩怪消失 → 死亡原图复活 → 约 8s 该点应回来（不必再打一场）。
2. 复活后立刻再打另一只 → 两只都应在战后约 8s 回出生点。
3. 普通野外战后刷新、世界 BOSS 永久 suppress 不变。
