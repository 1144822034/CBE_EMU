# Pixi 首战结算增量过大 + 战斗中黑屏

## 1. 结算经验/金钱偏大，且显示「(总)」

### 现象

第一次战斗胜利面板「经验 +N / 金钱 +N」接近角色累计总量；后面还带 `(总…)`。

### 根因

CBE `HandleBattleSettleMsg`：`4/7.exp/gold` 是**战后绝对总量**，面板获得量 = `new − old`
（`docs/re/2026-06-25-battle-server-flow.md`）。

Pixi 用 `expTotal - session.exp` 算增量，但选角 `actorinfo` 解析时跳过了 exp/money，
`session.exp/gold` 一直为 0 → 首战增量 = 总量。

### 修改

- `parseRoleSelectEnter` 写入 `exp`/`money`，进场景时种子 `session.exp/gold`。
- `1/1/21` 人物汇总同步基线。
- 结算文案只显示 `经验 +N` / `金钱 +N`，去掉 `(总…)`。

## 2. 战斗中突然一直黑屏

### 现象

战斗进行中单位/舞台突然变黑且不恢复。

### 根因

`paintUnitSprite` 的 pose 贴图缓存超过 96 条时，直接 `destroy` 最老纹理；该纹理仍挂在
单位 `Sprite` 上 → GPU 采样失效，角色呈黑块；长战斗动作帧多时必然触发。

### 修改

- 淘汰时跳过仍被单位 sprite 引用的 key。
- 地图异步加载加 `mapLoadGen`，避免竞态卸掉底图后不重绘。

## 验证

1. 新登录后首战：增量应为小额奖励（与 mock `exp_gain`/`gold_gain` 同量级），无「(总)」。
2. 单场多回合 / 挂机连战：单位贴图保持可见，不应再整屏发黑。
3. `npx tsc --noEmit`（PixiJS_web）。
