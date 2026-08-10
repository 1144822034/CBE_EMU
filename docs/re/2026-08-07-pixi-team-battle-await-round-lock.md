# Pixi 组队战：提交后等待播放锁

## 现象

组队战斗中玩家提交 `4/2` 后，服务端常先 `round_defer`（无 `4/6`）。客户端菜单已关，但按「确定」会把行动菜单重新打开，可再次操作。

## 契约

组队回合屏障（`docs/re/2026-08-05-team-battle-action-playback-chaos.md` 等）：本席已 act 后须等共享 `4/6` actioninfo 播放，再接受下一回合操作。

## 根因

`BattleScreen.confirm()` 在 `!menuOpen` 时无条件重开菜单；组队 defer 不触发 `applyUpdate`，等待期无锁。

## 修改

- `awaitingRound`：`noteOperateSubmitted` 置位（组队 / 切磋 / 单人 defer 同锁）；隐藏菜单、屏蔽 confirm/attack/skill/flee/cycle/计时普攻。单人同包 `4/6` 经 `applyUpdate` 清锁。
- `applyUpdate`（收到 `4/6`/逃跑/结算）清锁，再进入既有 `playbackBusy` 播放。
- 等待期软键置空。

> 2026-08-07：切磋亦走 `duel_round_defer`，不再仅限 `right.length >= 2`；见
> `2026-08-07-spar-merged-4-6-peer-hit-drop.md`。

## 验证

1. 双人组队：一人先出手 → 菜单消失且无法再开，直到合并 action 播放结束再出菜单。
2. 单人战：同包 `4/6` 立即播放（提交瞬间短暂锁，响应到后清锁）。
3. 切磋：双方出手后两段 `4/6` 均播放。
