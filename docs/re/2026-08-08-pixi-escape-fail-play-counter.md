# 逃跑失败后对方未继续行动

日期：2026-08-08

## 触发与现象

玩家逃跑失败时只提示「逃跑失败」，怪物/对方不播放行动、血量也可能不同步。

## 根因

服务端契约（`2026-06-25-battle-server-flow.md`）：

- 成功：`4/4 {result=1}` 单独结束战斗
- 失败：`4/4 {result=0}` + 同包 `4/6 actioninfo`（存活怪反击）

Pixi `parseBattleAction` 见到任意 `4/4` 就提前返回空 `actions`，丢掉失败包里的 `4/6`，播放层无脚本可播。

## 修改

`protocolExtras.parseBattleAction`：

- `result=1`：仍只返回逃跑成功并结束
- `result=0`：继续解析同包 `4/6`，带回反击 `actions`/`hpDeltas`，日志「逃跑失败 · …」

## 验证

1. 战斗中逃跑至失败：先见失败提示，再播对方攻击/技能，HP 随 actioninfo 变化。
2. 反击致死：出现复活确认（type-3）。
3. 逃跑成功：仍直接离场，无反击。
