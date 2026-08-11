# Pixi Android 切后台掉线（非挂机）

日期：2026-08-10（续：原生心跳深化）

## 现象

进图后按 Home / 切到其他 App（未开挂机），一段时间后角色被踢或回前台发现已断线。已开「后台无限制」仍掉。

## 根因

1. **前台服务只在挂机意图武装时启动**（早期）。普通在线仅靠 `MainActivity` 的 `PARTIAL_WAKE_LOCK` + `resumeTimers`。
2. 多数国产 ROM 在无前台服务时仍会冻结 WebView 定时器或掐断后台 `ws://`，scene-poll（约 2.5s）停发。
3. 服端 presence：`VM_MOCK_SERVICE_ONLINE_PRESENCE_MAX_AGE_TICKS=300`（`VM_SCHED_FRAME_MS=100` → **约 30s**）无 presence 更新则 `heartbeat-timeout` → `session_mark_offline`。
4. **即便已有 FGS + 无限制电池**：Chromium 在 `document.hidden` 时仍会大幅节流 `setInterval`，`resumeTimers()` 不够；scene-poll 停 → 仍超 30s presence。
5. **targetSdk 35 + `dataSync` FGS**：API 34+ 有约 6h/24h 时长上限，通宵挂机会被系统 `onTimeout`。
6. **未运行时申请 `POST_NOTIFICATIONS`**：Android 13+ 通知不可见时，部分 OEM 更易杀「看不见的」前台服务。

首个偏离：后台停心跳 → 服端踢下线；回前台只重开 WS 不能恢复已被清的 session。

## 修改

| 层 | 行为 |
|----|------|
| JS | 进图 `startPoll` → `syncNativeKeepAlive({ inWorld })`；挂机模式覆盖文案；退图/登出停服务 |
| JS | `window.__pixiNativeKeepAlive` → `GameSession.backgroundKeepAliveTick()`（poll 或 ping） |
| 原生 FGS | 每 **2s** `resumeTimers` + `evaluateJavascript` 注入心跳（不依赖 Chromium 定时器） |
| 原生 FGS | API 34+ 用 `specialUse`（避开 dataSync 6h）；更低版本仍 `dataSync`；`onTimeout` 自重启 |
| MainActivity | `onPause` 前后都 re-arm；暂停期每 2s 再 arm；申请通知权限；电池优化未开则每 3 天再弹 |
| poll / wake | 任意 poll 失败立即 `ensureConnected` 再试；`wakeFromBackground` / 切后台立刻补 tick |

## 验证

1. 登录进图（不开挂机）→ Home 切走 1–2 分钟 → 通知栏见「江湖OL 游戏中」→ 切回仍可移动。
2. 开挂机 → 通知变为「挂机中」；停挂且仍在图 → 回到「游戏中」。
3. 息屏 / 切后台 **10+ 分钟**（无限制电池 + 允许通知）→ 仍在线或挂机继续。
4. 退出登录 → 前台通知消失。
5. 首次安装允许通知权限；系统设置对该应用关电池优化。

## 残留风险

- 强杀进程 / 极端省电 ROM 仍可能掉线；挂机硬恢复路径不变。
- 进图期间常驻通知属预期（FGS 契约），无法在无通知下可靠保活。
- `specialUse` 适合侧载；若上架应用商店需按商店政策补充声明。
