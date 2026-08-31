# Pixi：部分机型战斗/挂机动画偏快（2026-08-30）

## 现象

不同手机上战斗待机扇翅、攻击条、地图挂机怪待机动画快慢不一致；高刷机（90/120Hz）上明显偏快。

## 根因

与 `2026-08-09-pixijs-walk-speed-hz.md` 同类：逻辑按「每次 ticker 回调」或依赖不稳的 `deltaMS` 时，刷新率越高单位时间推进越多。

具体：

1. Pixi ticker 默认不限帧，高刷 WebView 以显示刷新率回调。
2. `BattleScreen.tick` / 地图 `ActorLayer` 待机累加器在越过阈值后 **reset=0**（丢掉余量），且战斗侧直接吃 `ticker.deltaMS`，未像走路那样用墙钟 `performance.now()` 自算并 clamp。
3. 传送门等处同样 reset 累加器。

## 修改

| 文件 | 行为 |
|------|------|
| `main.ts` | `app.ticker.maxFPS = 60`（对齐走路 ~60Hz 手感） |
| `BattleScreen.ts` | 自算墙钟 `dt`（clamp 50ms）；idle/combat 累加器 `while` 扣减 |
| `ActorLayer.ts` | 走/待机累加器 `while` 扣减，可一次补多步 |
| `SceneView.ts` | 入口统一 clamp `dt`；传送门累加器扣减 |

播报 `sleep(ms)` / `poseUntil` 仍走墙钟，不受本改影响。

## 验证

1. 60Hz 与 120Hz（或 Chrome「帧率限制」）同场景：战斗 idle 扇翅、挂机地图怪待机周期应接近（约 360ms/拍）。
2. 攻击条仍约 `parts × 100ms`；回合倒计时仍约 1s/格。
3. 走路速度回归 `2026-08-09`：按住方向路程/秒仍接近。
