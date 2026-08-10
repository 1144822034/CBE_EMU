# 2026-08-10 战后左上角血蓝条未刷新

## 现象

战斗胜利/结算回到地图后，左上角 `WorldHud` HP/MP 条仍是战前值，或不含结算回血蓝。

## 根因

1. `finished` 过早 `hide` 打断延后 vitals（已用结算确认 / `onExitReady` 修）。
2. **`4/7.hp` / `4/7.mp` 是回复量（auto-flask / recover），不是绝对血蓝。** Pixi 未解析、未加到战斗座位，回地图仍用战末受伤值；服务端角色已是回复后值。
3. **战后权威同步是 group `5/11` hsp**（普通胜利不推 `1/1/14`）。Pixi 只改 `teamMembers`，未写 `session.hp` / `onVitals`。

## 修改

- 解析 `4/7` recoverHp/recoverMp；播完结算前加到己方座位再 `notifyVitalsChanged`。
- `5/11` / 多人 `5/10` 命中自身 → `emitVitals` → HUD。
- `endBattleToWorld` 仍在 `hide` 前后用座位/session 重绘左上角。

## 验证

1. 受伤打赢（有壶自动回血）→ 回地图左上角接近满血，与壶回复一致。
2. 无壶残血胜利 → 回地图为战末残血，不是战前满条。
3. 组队战后 `5/11` 到达时左上角跟随更新。
