# 普通野怪进战后地图不消失 / 战后立刻还在

日期：2026-08-30

## 现象

玩家与地图小怪进战（碰撞 / 点击 / 挂机）后，地图上的 SCE 刷怪点不消失，或一回地图就立刻还在，没有「隐藏 → 约 10s 回出生点」的节奏。

## 预期

- 开战：对应 SCE 标记立即从地图移除（临时 suppress）。
- 开战失败：立刻还原。
- 战后：约 10s 后在出生点重新出现（`armSuppressedRespawns`）。
- 世界 BOSS 仍只走 `[WBOSS:claim]` / despawn 永久移除。

## 根因

1. **`onBattle` 只在 `session.sceneMonsters` 里按坐标找 hideId**  
   战后该列表常是 `liveSceneMonsters()`（已过滤 suppress）。挂机下一场或会话列表过期时对不上点 → **从未 suppress**。

2. **`setSceneMonsters` 按 actorId 丢 suppress**  
   战后 `refreshSceneCombatSpawns` 按 `0x70000000+i` 重编号；世界 BOSS 增删会挪动下标。旧 suppress 键对不上就被删掉 → 全表重画 → **刚打过的怪立刻出现**。

3. **回滚条件用 `!session.inBattle`**  
   开战成功且战斗已结束时，`startBattleNearPlayer` 的 Promise 若晚于 `endBattleToWorld` 才 settle，会把已 suppress 的怪 **误 restore**。

4. **挂机 / 挑战跳过 collide**  
   未在 4/1 武装时立刻清图，仅依赖脆弱的 `onBattle` 匹配。

首次偏离：进战成功后地图标记应临时隐藏；因匹配失败或 suppress 被 SCE 刷新冲掉，表现为「一直在 / 不会消失再刷新」。

## 修改

- `ActorLayer.suppressSceneMonsterAt`：按出生点 / 脚印清图。
- `setSceneMonsters`：按 home 坐标重绑 suppress，不因 id 漂移清空。
- `upsert`：suppress 期间禁止异步重建 SCE 标记。
- `startBattleNearPlayer` → `boolean`；`onEngageSceneMonster` / `onRestoreSceneMonster`；失败才还原。
- `endBattleToWorld`：先 `armSuppressedRespawns(10s)`，再异步 SCE 刷新并二次 arm。

## 验证

- [ ] 踩普通野怪：进战地图标记立刻消失；战后约 10s 回出生点。
- [ ] 点击 SCE 怪：同上。
- [ ] 挂机遇怪：开战清图 + 10s 回刷；开战失败怪立即回来。
- [ ] 同图有世界 BOSS 时打普通怪：普通怪仍临时隐藏，不被 BOSS 刷新打乱。
- [ ] 世界 BOSS claim：仍永久消失（不 10s 回刷）。
