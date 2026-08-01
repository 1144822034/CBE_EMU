# 客户端启动连服卡顿：scene-poll 堵队列

## 现象

- ICMP ping `47.108.210.191` 延迟很低。
- 客户端日志：`worker started` 后长时间无输出，随后
  `scene-poll` failed，再出现
  `queue_data ... queue_ms≈20791 network_ms≈263`。
- 服务端对应版本/登录 `process_ms` 仅 0–8ms。

## 根因

1. 客户端只有一个 network worker，FIFO。
2. 启动后 `scene-poll` 可抢先入队；`connect()` 为阻塞调用，Windows 上 SYN
   无应答默认约 21s 才失败，`SO_RCVTIMEO` 不限制握手。
3. 版本等 WT data 卡在 poll 后面，`queue_ms` 接近 21s；真正联网仅数百 ms。

## 修改

`src/network-client.c`（jni 同步副本同改）：

- `connect` 改为非阻塞 + `select`，握手超时 `VM_CLIENT_CONNECT_TIMEOUT_MS=2000`。
- 首次成功 WT data 前不入队 scene-poll（`serviceReachable`）。
- WT data 入队时插到已排队的 scene-poll 之前。
- 点分 IPv4 字面量强制 `AF_INET`，避免双栈空等。

## 验证

复测启动进标题：

- 首条 `queue_data` 的 `queue_ms` 应接近 0（或远小于 21s）。
- 不应再先出现长时间无输出后的 `scene-poll` failed 再出版本包。
- 正常进场景后 scene-poll 仍应工作。

## 后续：登录后「网络超时」与 poll 风暴（同日）

登录 `resp=112` 成功后客户端连续 `scene-poll`/`data` failed，服务端不再出现新
请求日志。原因是 title 阶段每个调度 tick 都可发起 scene-poll，失败时仍快速重试，
对远程 `47.108…:19090` 形成短连接风暴，后续业务包（角色列表等）也连不上，客户端
提示网络超时。

追加客户端节流（`network-client.c`）：

- scene-poll 至少成功 3 次 WT data 后才开启；有 data 排队时不发 poll。
- poll 最小间隔 1s；失败指数退避并暂时关闭 `serviceReachable`。
- poll / data 使用不同 connect 超时（1.2s / 4s）。
