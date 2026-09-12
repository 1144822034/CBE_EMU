# Pixi 挂机成对开战 / 场间不均

日期：2026-09-12

## 现象

Android APK 挂机：连续两场战斗 → 隔一段 → 再连续两场；体感不是「每场结算离场后再等固定间隔」。

复测（场间改为 8000 后）：几乎总是 **约 13 秒** 再进下一场（≈ 8s 场间 + 5s 失败重试）。

## 根因

Pixi 挂机是客户端 `4/1` 环（`scheduleNextHangupEncounter` → `tickHangupEncounter`），与服端 `2/10` HangupLoop 无关。

1. **双触发竞态**：前台/回前台用 `setTimeout`；息屏用 FGS `__pixiNativeKeepAlive` → `driveBackgroundHangup` 看 `hangupEncounterDueAt`。到期时两者可同时进入 `tickHangupEncounter`，在 `inBattle=true` 之前各发一次 `4/1` → 成对开战。失败侧还会再 `schedule` 一次重试，加重成对感。
2. **场间过短（已调）**：客户端默认曾为 **4000ms**；现与发奖闸门对齐为 **8000ms**。
3. **时钟**：`dueAt` 曾用 `performance.now()`；WebView 后台可能冻住该时钟，与墙钟 FGS 不同步 → 改 `Date.now()`。
4. **13s 叠时（复测）**：离场后 8s 首次 `4/1` 常被判失败（战后 live SCE 仍在 10s suppress；或 `started` 只扫原始 4/5/4/10，遇 tear-down 前缀包误判），再 `schedule(5000)` → **8+5≈13s**。空 `combatSpawns` 的 post-battle refresh 还会误清空 seed catalog。

首个偏离：同一挂机槽位被 schedule 与 native-keepalive 各消费一次；或首次 4/1 误失败后叠长重试。

## 修改

| 项 | 行为 |
|----|------|
| `HANGUP_ENCOUNTER_INTERVAL_MS` | **8000**（结算离场后地图侧等待） |
| `HANGUP_ENCOUNTER_RETRY_MS` | **1500**（避免 8+5≈13） |
| `hangupEncounterEarliestAt` | 离场锚点；重试不得早于场间 |
| `hangupEncounterInFlight` | 串行化 `tickHangupEncounter` |
| `hangupEncounterDueAt` | `Date.now()` 墙钟 |
| `startBattleNearPlayer` | `parseBattleStart` 与 raw 4/5\|4/10 双判成功 |
| `adoptSceneCombatSpawns` | 空列表不覆盖已有 catalog |
| hangup tick | live 空时用 catalog 种子再 `4/1` |

## 验证

1. APK 开挂机：离场回地图到下一场开战约 **8s**（亮屏另加结算面板 ~2.5s 属结算窗，不计入场间）。
2. 不应再稳定出现 13s，也不应「两场一簇」。
3. 开战失败：约 1.5s 后重试，且仍不早于离场+8s。
4. 控制台 `[session] hangup encounter` 同一时刻不应连续两条不同 reason 都进战。
