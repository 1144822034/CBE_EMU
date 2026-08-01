# 胜利结算改为离场后再显示 — 已回退

Date: 2026-07-28

Status: **rejected / reverted** — 客户端契约不支持 exit-then-4/7

```text
phase: try kill 4/6 only -> 4/8 exit -> post-exit 4/7
trigger: 用户希望退出战斗场景后再显示结算
runtime: 没离场就看到结算感 / 离场后空框
```

## 运行时证据（用户日志）

挂机胜利一场完整链：

1. `mock_battle_operate ... resp=117` — 仅 `4/6`，`inline_settle=0`
2. `settlement_exit ... post_exit_settle=1 response=4/8+4/11+4/9`（resp=104）
3. `post_exit_settle ... response=4/7`（resp=550，`exp_gain=10` 非零）

用户体感：**离场后空框**；结算内容时机也不对。

另：前两场触怪在 `exit_pending` 未到期时又 `challenge`/`hangup` start，
触发 `clear-allow-reenter`，结算链被清掉（旁证，非本根因）。

## 根因陈述

- **触发条件**：先发 `4/8` 再发 `4/7`。
- **被违反的契约**：`HandleServerBattleCmd` case8 → `BattleSettle_UpdateCharAttrs`
  依赖先前 `4/7` 填好的结算缓存；lone `4/8` 刷空缓存 → **空白提示框**
  （与 `2026-07-28-pve-settlement-blank-prompt.md` 同类）。
- **首个错误状态**：拆场包到达时 settle cache 为空。
- **post-exit `4/7`**：不是地图侧结算 UI；Battle.cbm 已拆或正在拆，无法替代
  「战内先 `4/7` 再离场」的权威路径。

## 结论

`4/7` 结算面板属于 Battle.cbm 生命周期，必须在 `4/8` **之前**下发。
「先离场再结算」不能靠调包序实现；若要地图结算，需另取证非 `4/7` 契约
（如系统消息 / 其它 WT），当前 **unresolved**。

## 回退契约（当前）

```
胜利：4/6 + 4/7（内联）
  → playback + panel_ms
  → poll：4/8 + 4/11 type=0 + 4/9
```

`CBE_BATTLE_INLINE_SETTLEMENT` 默认恢复 **1**。post-exit settle poll 禁用。
