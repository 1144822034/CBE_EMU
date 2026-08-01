# Android 锁屏 / 切后台保持连接与自动重连

日期: 2026-07-29

## 现象

安卓端锁屏、黑屏或切到后台后，江湖 Online 容易掉线。服务端日志常见 `heartbeat-timeout` 后 `session_offline`。

## 根因

1. **进程冻结**：客户端仅有 Activity + 匿名线程，无前台服务 / WakeLock。屏幕关闭后 CPU 可进入休眠，模拟器调度与 scene-poll 心跳停止。
2. **服务端契约**：在线存在依赖近期 presence（`VM_MOCK_SERVICE_ONLINE_PRESENCE_MAX_AGE_TICKS`）。心跳停发后会话被标离线。
3. **传输层恢复缺口**：`serviceReachable` 在连续传输失败后置 false，并直接禁止后续 scene-poll。待机无 WT data 时无法自行恢复，形成“永久掉线”。

首个偏离点：宿主进程停止发出 scene-poll / WT 请求 → 服务端 presence 过期 → 踢下线。崩溃或 UI 异常不是根因。

## 修改

### Android 常驻

- 新增 `GameKeepAliveService`：前台服务 + `PARTIAL_WAKE_LOCK` + Wi-Fi lock。
- `MainActivity` 在模拟器启动后拉起服务；退出 / `onDestroy` 时停止。
- Manifest：`WAKE_LOCK`、`FOREGROUND_SERVICE`、`FOREGROUND_SERVICE_DATA_SYNC`、`POST_NOTIFICATIONS`、`REQUEST_IGNORE_BATTERY_OPTIMIZATIONS` 等。
- 首次启动可提示忽略电池优化（OEM 后台限制）。

### 网络自动重连（`network-client.c`，桌面与 JNI 同步）

- WT data：重试 5 次，失败间隔 `250ms * attempt`。
- 登录已成功（`successfulDataCount >= 3`）后，即使 `serviceReachable == false`，backoff 到期仍允许 scene-poll 作为 reconnect probe。
- scene-poll 成功时恢复 `serviceReachable`。

## 验证计划

1. 登录进场景后锁屏 / 关屏 2–5 分钟，再解锁：角色仍在线，可移动。
2. Home 切到其他 App 后切回：不断线。
3. 飞行模式开几秒再关：日志可见 `reconnect_probe` / `reconnect_recovered` 或 `data_request recovered`，随后心跳恢复。
4. 主动点退出：前台通知消失，服务端收到 disconnect，无残留假在线。

## 残留风险

- 部分国产 ROM 仍可能强杀带前台服务的进程；需用户关闭电池优化 / 允许自启动。
- 本修复保持宿主传输与心跳；不伪造登录态。若服务端已踢下线且客户端会话态已清，仍需重新登录。
- targetSdk 33；升到 34+ 时需复核 `foregroundServiceType=dataSync` 与通知权限行为。
