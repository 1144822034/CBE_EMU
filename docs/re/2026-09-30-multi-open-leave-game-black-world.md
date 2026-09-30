# 多开 leave_game 后另一号世界黑屏

日期: 2026-09-30

## 触发

1. A、B 多开；B 菜单「离开游戏」。
2. 自动切回 A：非战斗地图黑屏；进战斗正常。

## 根因

1. `leave_game` 对**共享** `SceneView` 调用 `invalidateMap()` / `actors.clear()`。
2. 若 A 当时在战斗中，`switchAccountSlot` 旧路径只 `flushPendingBattle` 就 return，**不**走 `onEnterScene` 重装地图。
3. 战斗 UI 不依赖地图 → 正常；战后 `showPhase("world")` 时 `mapSprite` 已空 → 黑屏。

## 修改

| 位置 | 改动 |
|------|------|
| `leave_game` | 仍有其他槽时不 wipe 共享地图 |
| `switchAccountSlot` | 有 `session.scene` 时一律先 `onEnterScene`，再视情况 flush 战斗 |
| `endBattleToWorld` | `!showsSceneFile` 时补一次 `onEnterScene` |

## 验证

1. A 挂机/战斗中，B 离开游戏 → 切回 A 世界应有地图；若仍在战，出战后地图正常。
2. A 在城镇地图时 B 离开 → A 地图正常。
3. 单号离开游戏仍回登录页。
