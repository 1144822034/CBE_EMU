# 多开战斗界面切号黑屏

日期: 2026-10-09

## 触发

1. 同页多开，当前号在战斗界面点顶/底账号芯片切到另一号。
2. 尤其：战斗中 → 另一号也在战斗，或战斗中 → 另一号在同一张地图的世界。
3. LCD 整屏黑一段时间，技能/背包请求慢时更明显；切到非战斗号可能一直黑。

## 根因

1. `switchAccountSlotBody` 先 `closeOverlaysForAccountSwitch()` → `battle.resetForAccountSwitch()` / `hide()`：`BattleScreen.visible=false` 且拆掉地图/单位，但 **`phase` 仍是 `"battle"`**，`showPhase` 未跑，`view.root.visible` 仍是 false。
2. 随后一律 `await onEnterScene`（2026-09-30 为战后地图补的重装）。同图进入走「keep battle over same-scene enter」：条件是 `(phase === "battle" || session.inBattle) && showsSceneFile && hp>0`，**不看战斗 UI 是否还在**。
3. 提前 `return` 既不 `showPhase("world")` 也不把战斗画回来。下一号若在打，要等 `onBattle` 里 `getBattleSkills` / `openBag` 的异步 IIFE 结束才 `battle.show` + `showPhase("battle")` → 中间整屏黑。下一号若不在打，这条路径没有 flush 战斗，**会一直黑**。

协议/会话本身仍在；这是显示层 `phase` 与 `battle.visible` 失步，不是 WebGL lost 的唯一解释（VRAM 尖峰仍可能叠加）。

## 修改

| 位置 | 改动 |
|------|------|
| `main.ts` `onEnterScene` | keep-battle 增加 `battle.visible`：只有本屏战斗 UI 还在才压住世界 |
| `main.ts` `switchAccountSlotBody` | `onEnterScene` 之后若仍 `phase==="battle" && !battle.visible`，先 `showPhase("world")` 再 flush |
| `BattleScreen.hide` | `mapLoadGen++` 并清 `battleMap*`，避免 hide 后过期 `b_*.sce` 再挂上已销毁的 host |

## 验证

1. A 战斗中切 B 战斗中：应先见到 B 的世界图（短），再出 B 的战斗；不应长时间整屏黑。
2. A 战斗中切 B 世界（同图或不同图）：应直接出 B 地图，不再卡黑。
3. 单号同图 30/1 / 死亡确认：战斗 UI 仍在时，进场景不得把 mmBattle 拆掉换成世界。
4. A 战斗中 B `leave_game` 切回 A：战后世界仍有地图（2026-09-30 契约）。
