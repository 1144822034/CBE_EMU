# Pixi 挂机保活 + 断线自动重挂（2026-08-10）

## 现象

玩家开启挂机后息屏/睡觉：WebView 定时器或网关 WS 掉线 → scene-poll 停 → 服端约 30s presence 超时清 session 挂机定时器 → 挂机环中断。现有客户端只 `ensureConnected`/ping，不会重发 `2/10`。

## 根因

1. 挂机下一场只在 scene-poll 投递（`build_pending_hangup_loop_battle_response`）。
2. `session_mark_offline` 清 session auto timers；登录**故意不续挂**（见 `2026-08-01-session-auto-timer-authority.md`）。
3. 客户端无「挂机意图」持久化，也无硬断后自动重登进图再 `2/10`。

## 修改（不改服端离线代打契约）

| 层 | 行为 |
|----|------|
| 意图 | `hangupDesired` + `localStorage pix_hangup_resume`（账号/服/角色/技能） |
| 保活 | 挂机意图武装时 APK 前台服务 `HangupKeepAlive` + 现有 WakeLock/`resumeTimers`；poll 失败立即 `ensureConnected` 再试 |
| 软恢复 | `wakeFromBackground` / poll 从错误恢复后，若意图在且 `!hangupActive && !inBattle` → `startHangup()` |
| 硬恢复 | 连续 poll 失败≥3 或冷启动读到快照 → 登录→选服→选角→`startHangup`（30s 冷却） |
| 停挂 | `stopHangup()`：先 `clearHangupDesired`，战内即使 `hangupActive` 不同步也发 `2/10`；地图仅清意图不误发开挂 |
| 顶号 | `account-kicked` 清意图，不与另一端抢登 |

## 续：战内点关闭停不掉（2026-08-10）

### 现象

保活上线后，战斗界面点「关闭」仍继续挂机（本场打完又遇怪，或息屏恢复后又挂上）。

### 根因

1. 战内关闭走 `startHangup()`，以 `hangupActive` 区分开/停。
2. 保活引入 `hangupDesired` 后，战内可能出现 `battle.hangupActive=true` 但 `session.hangupActive=false`（chat 回调先于 `applyHangupChatFlag`，或清角色态只清了 active）。
3. 此时 `startHangup` 走**开挂**分支：`markHangupDesired()` + `2/10`；服端若环仍在则回「下一场完成后挂机停止」，但客户端意图又被武装 → 传输恢复/`tryRearmHangup` 再次 `2/10`。

### 修复

- 新增 `GameSession.stopHangup()`：无条件清意图；`hangupActive || inBattle` 才发停环 `2/10`。
- 战内关闭 / HUD 停挂改调 `stopHangup`；HUD 在 `hangupDesired` 时也走停/取消恢复。
- `handleBodies` 先 `applyHangupChatFlag` 再 `onChat`；「下一场完成后挂机停止」也清意图。

## 验证

1. 挂机后息屏 5–10 分钟（APK + 忽略电池优化）：仍遇怪；通知栏见「挂机中」。
2. 飞行模式数秒再开：tip「连接恢复，正在恢复挂机…」+「已开始挂机」。
3. 杀进程再开：静默重登进图并重挂。
4. 挂机中点停止 / 战内关闭：停止后**不会**被自动重挂。
5. 城内误挂「不能挂机」：清意图，不循环重试。
6. 挂机进战点「关闭」→ tip「下一场完成后挂机停止」或「已停止挂机」；本场后不再遇怪；息屏唤醒也不重挂。

## 残留风险

- 国产 ROM 仍可能强杀；需用户允许自启动/电池无限制。
- 本机 `localStorage` 含密码，与登录表单同级风险。
- 死亡导致「已停止挂机」保留意图但不立即重挂；仅传输恢复路径会重发 `2/10`。
