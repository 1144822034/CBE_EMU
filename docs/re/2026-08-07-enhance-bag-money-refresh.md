# 强化/重铸后背包金币不刷新

Date: 2026-08-07

## 症状

强化（29/3）或重铸转移（29/6）扣铜后，返回背包时金/银/铜仍显示扣款前余额。

## 根因

服务端已在成功路径同包追加 `1/10/26 { money=role->money }`（与丢弃/出售同源），但 Pixi `handleBodies` 未解析该对象，`characterStats.money` / `session.gold` 仍为旧值；`onBag` → `setBagMeta` 继续读旧余额。

## 修复

- `parseMoneyBalanceSync`：读 `10/26.money`
- `GameSession.handleBodies`：更新 `gold` + `characterStats.money`，触发 `onCharacterStats` → `setBagMeta`
- `closeEnhanceToBag`：回背包时再用当前余额刷一次 meta

## 验证

强化/重铸成功后返回背包，底部金/银/铜应等于扣费后权威铜币。
