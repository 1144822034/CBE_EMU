# Pixi Android 切后台挂机停手 / 不打怪

日期：2026-08-12

## 现象

Android APK 进图开挂机（或战斗中 [自动]）后按 Home / 切到其他 App：人物动作停住，不再遇怪、战斗中也不再出手。回前台后往往恢复。通知栏「挂机中」仍在，网关未必断线。

## 根因

既有息屏保活（`HangupKeepAliveService` + `backgroundKeepAliveTick`）**只维持 scene-poll / WS**，不驱动 Pixi 客户端挂机环。

Pixi 挂机契约（与 CBE 服端 `2/10` 无关）：

1. 下场遇怪：`scheduleNextHangupEncounter` → `setTimeout` → `startBattleNearPlayer`（4/1）
2. 战中出手：`armInBattleAuto` / `onAutoReady` → `setTimeout` → `submitAutoOperate`（4/2）
3. 回合播报：`playActionScript` → `sleep()` → `setTimeout`；未播完则 `playbackBusy`，`canSubmitOperate()` 为假
4. 结算离场：`armSettleAutoClose` 2.5s `setTimeout`

Chromium 在 `document.hidden` 时大幅节流 `setTimeout`；FGS 每 2s 注入的 `__pixiNativeKeepAlive` 原先只 `pollOnce()`，故：

- 在线心跳仍可能正常（presence 不超时）
- 遇怪定时器 / 出手定时器 / 播报 sleep / 结算定时器全部饿死 → **不打怪**
- Pixi ticker 停 → 地图/战斗帧动画暂停（视觉症状，非协议根因）

首个偏离：切后台后第一个依赖 `setTimeout` 的挂机步骤不再按时触发；崩溃/掉线不是本题必要条件。

## 修改

| 层 | 行为 |
|----|------|
| `BattleScreen.sleep` | `document.hidden` 立即结束；监听 `visibilitychange`；导出 `flushBattleSleepsForBackground` |
| `BattleScreen` | `confirmSettlementIfAwaiting`；后台结算 delay=0 |
| `GameSession` | 挂机截止时间 `hangupEncounterDueAt`；`driveBackgroundHangup`；`onBackgroundGameplayTick` |
| `main.ts` | FGS tick：flush 播报 → 结算确认 → `fireInBattleAuto`；再 poll；再检查挂机截止 |

## 验证

1. 开挂机 → Home 切走 2–5 分钟 → 回前台：等级/经验应有增长，或仍在战斗中；不应长期停在同一场播报卡死。
2. 仅 [自动]、手动进怪后切后台：应继续 4/2。
3. 不开挂机仅挂机 → 仍只保活在线（回归 `2026-08-10-pixi-android-background-disconnect.md`）。
4. 前台挂机结算仍约 2.5s 自动关闭（未改前台 UX）。

## 残留

- 后台时地图走跑帧仍会停（无 ticker）；属显示层，不影响协议挂机。
- 强杀进程 / 极端省电 ROM 仍可能冻 JS；需系统对该应用关电池优化并允许通知。
