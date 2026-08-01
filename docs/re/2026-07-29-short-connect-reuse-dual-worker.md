# 短连接优化：连接复用、双 worker、更快超时

日期: 2026-07-29

## 背景

宿主传输是 **短 TCP**：每次 WT data / scene-poll 历史上都是 connect→CBMS→CBMR→close。
玩家反馈卡住/掉线，常见叠加因素：

1. scene-poll 与 WT data 共用单 worker，一次 `connect()` 黑洞会堵死整条队列。
2. 每次握手放大远程 RTT，操作连点时体感卡顿。
3. 服务端默认 4 worker，多端短连易排队 / `queue-full`。
4. 锁屏心跳停发（另见 `2026-07-29-android-keepalive-reconnect.md`）。

## 已实现

### 1. 立刻项（服务端容量）

- `CBE_MOCK_SERVICE_WORKERS` 默认 **8**，上限 **32**（原 4 / 16）。
- 仍可用环境变量覆盖。

### 2. 客户端双 worker（data / poll 隔离）

- `network-client.c`：独立 `dataWorker` + `pollWorker` 与双队列。
- scene-poll 不再与登录/操作包抢同一条 FIFO。
- 启动日志：`dual workers started data+poll ...`

### 3. 连接复用（简易长连接窗口）

- 客户端 `data` / `poll` 各持一条 live TCP session；成功后不立刻 close。
- 服务端处理完一帧后 `select` 等待 `SESSION_IDLE_MS=400`：有下一 CBMS 则同连接继续，否则关 socket 释放 worker。
- 400ms 窗口覆盖连点 WT 突发，又不把 ~3s 的 poll 绑死成「一玩家一 worker」。
- 日志：`session_reuse frames=...`（服务端）、`data_session reused`（客户端节流）。

### 4. 更快失败

- WT data `connect` 超时 **3000ms**（不再无限阻塞 OS SYN）。
- poll connect 仍 **1500ms**；失败带阶段日志与递增重试间隔。

### 5. 失败可观测

- `transport_unavailable`：连续失败把 `serviceReachable` 打掉时打印 connect/queue/network 耗时。
- `reconnect_probe` / `reconnect_recovered`：恢复路径保留。

## 验证

1. 进场景后连续操作：服务端偶发 `session_reuse frames>=2`，客户端不应再因 poll 堵登录。
2. 拔网 / 飞行模式数秒：见 `transport_unavailable`，恢复后 `reconnect_*` 或 `*_request recovered`。
3. 多客户端压测：默认 8 worker 下 `queue-full` 应明显少于旧默认 4。
4. `make -j2` 通过；安卓 JNI 已同步同一份 `network-client.c`。

## 心跳间隔（2026-07-29）

客户端 `VM_CLIENT_POLL_MIN_INTERVAL_MS` 由 1000 调整为 **3000**。
服务端 presence 超时仍约 30s，余量充足。附近人/组队等 poll 投递最坏多等约 2s。

## 弱网超时（同日）

| 常量 | 原值 | 现值 |
|------|------|------|
| `VM_CLIENT_SOCKET_TIMEOUT_MS` | 8000 | **12000** |
| `VM_CLIENT_DATA_CONNECT_TIMEOUT_MS` | 3000 | **5000** |
| `VM_CLIENT_POLL_CONNECT_TIMEOUT_MS` | 1500 | **2000** |
| `VM_CLIENT_POLL_MIN_INTERVAL_MS` | 3000 | 3000（未改） |

## 残留风险

- 仍非全会话真·长连接；空闲 >400ms 会拆 TCP。要「始终一条 socket」需异步/每会话线程模型，避免 worker 被 idle recv 占满。
- 协议层仍靠 scene-poll 刷新 presence；安卓需配合 keepalive。
- 未伪造 guest 断线事件；会话若已被服务端 `heartbeat-timeout` 踢掉，仍需重新登录。
