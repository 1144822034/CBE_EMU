# Pixi Android 切后台掉线（非挂机）

日期：2026-08-10

## 现象

进图后按 Home / 切到其他 App（未开挂机），一段时间后角色被踢或回前台发现已断线。

## 根因

1. **前台服务只在挂机意图武装时启动**（`HangupKeepAlive`）。普通在线仅靠 `MainActivity` 的 `PARTIAL_WAKE_LOCK` + `resumeTimers`。
2. 多数国产 ROM 在无前台服务时仍会冻结 WebView 定时器或掐断后台 `ws://`，scene-poll（约 2.5s）停发。
3. 服端 presence：`VM_MOCK_SERVICE_ONLINE_PRESENCE_MAX_AGE_TICKS=300`（`VM_SCHED_FRAME_MS=100` → **约 30s**）无 presence 更新则 `heartbeat-timeout` → `session_mark_offline`。
4. 客户端 `pollOnce` 的 `ensureConnected` 重试原先仅挂机路径执行；回前台 `wakeFromBackground` 只 ping，不立刻 scene-poll。

首个偏离：后台停心跳 → 服端踢下线；回前台只重开 WS 不能恢复已被清的 session。

## 修改

| 层 | 行为 |
|----|------|
| JS | 进图 `startPoll` → `syncNativeKeepAlive({ inWorld })`；挂机模式覆盖文案；退图/登出停服务 |
| 原生 | `HangupKeepAliveService` 支持 `online` / `hangup` 通知文案 |
| MainActivity | 暂停期间每 4s 再 `onResume`+`resumeTimers`，防止 OEM 再次 pause WebView |
| poll / wake | 任意 poll 失败立即 `ensureConnected` 再试；`wakeFromBackground` 在在线时补一次 poll |

## 验证

1. 登录进图（不开挂机）→ Home 切走 1–2 分钟 → 通知栏见「江湖OL 游戏中」→ 切回仍可移动。
2. 开挂机 → 通知变为「挂机中」；停挂且仍在图 → 回到「游戏中」。
3. 退出登录 → 前台通知消失。
4. 系统设置对该应用关电池优化（首次仍会弹忽略优化）。

## 残留风险

- 强杀进程 / 极端省电 ROM 仍可能掉线；挂机硬恢复路径不变。
- 进图期间常驻通知属预期（dataSync FGS 契约），无法在无通知下可靠保活。
