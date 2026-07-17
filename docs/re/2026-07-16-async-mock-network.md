# Mock 网络异步收发

## 问题

远程 mock 模式原先在 CBE 的网络发送钩子中直接执行 `connect/send/recv/close`。服务端处理一条高频 `2/1 moveinfo` 时，客户端模拟器线程会一直等待响应；2026-07-16 的运行日志中，六条移动请求的服务端平均处理时间为 `674.3 ms`（`599..961 ms`），因此每次 `net_queue_data ... resp=11` 都对应一次明显停顿。`resp=11` 是响应字节数，不是耗时。

## 实现

第一阶段保留现有“一请求一个 TCP 连接”的服务协议，只移动阻塞边界：

1. `vm_net_mock_on_send` 在模拟器线程读取请求字节并放入有界 FIFO，立即返回给 CBE；
2. 单个后台工作线程严格按入队顺序执行远程请求；
3. 工作线程只操作宿主内存和 socket，不调用 `vm_malloc`、`uc_mem_write` 或 CBE 回调；
4. 工作线程把独立的主响应、follow-up 响应和关闭标志放入完成队列；
5. `scheduler_tick` 在模拟器线程排空完成队列，分配 VM 缓冲并正常投递 `event=7`；
6. scene-sync poll 进入同一个 FIFO，并且最多只允许一个未完成 poll，避免服务端忙时仍从模拟器线程同步等待；
7. runtime restart 使用 generation 丢弃旧完成项，进程退出时停止并 join 工作线程。

队列容量为 64。普通请求与 poll 共用单工作线程，所以同一客户端的服务端状态变更和响应顺序保持不变。

## 运行日志

异步启动：

```text
net_async_worker started queue_cap=64 transport=per-request-tcp
```

完成投递包含三个宿主侧耗时：

```text
net_queue_data ... async_queue_ms=N network_ms=N deliver_ms=N
```

- `async_queue_ms`：请求在 FIFO 中等待工作线程的时间；
- `network_ms`：连接、服务端处理、接收响应的总时间；
- `deliver_ms`：响应完成后等待模拟器调度 tick 的时间。

## 验证

- `gcc -c src/main.c` 独立编译通过；
- 异步客户端冒烟正常收到启动响应：`network_ms=122, deliver_ms=75`；
- `--autotest --max-ms=12000` 正常执行 `host_quit_request`、屏幕销毁、工作线程 join，并以退出码 0 结束；
- 冒烟记录：`tmp/async-network-exit-smoke-20260716-145119/`；
- `make -j2` 通过；
- 正式服务使用新构建重启，PID `15700`，监听 `127.0.0.1:19090`；日志为 `tmp/mock-service-19090.20260716-145610.stdout.log`。

## 后续

异步化消除了服务端等待对客户端帧循环的直接阻塞，但没有缩短服务端本身的处理时间。当前服务端 stdout 为无缓冲模式，高频移动日志仍可能让 `network_ms` 达到数百毫秒并造成 FIFO 累积。下一步应对 moveinfo 日志限频或改为缓冲/异步日志；稳定后再评估持久 TCP 连接。

## 2026-07-17 场景切换接收超时

临安场景出口切换复现了：服务端已生成 `2/3` 响应，但客户端后台请求报告
`pending=game-timeout`，服务端紧接着记录 `dropped malformed request`。同一日志中
普通 `2/1 moveinfo` 已出现 `process_ms=1026`，而远程 socket 的收发超时固定为
`1500 ms`。场景切换还需要捕获会话和 MySQL 状态，超过该窗口后客户端先关闭
socket，服务端发送响应头/正文时失败，因此没有形成正常的完成队列项。

`VM_MOCK_SERVICE_SOCKET_TIMEOUT_MS` 现设为 `5000 ms`。这只扩大后台工作线程的
远程请求等待窗口，不会重新阻塞 CBE/模拟器帧循环；连接拒绝仍会立即失败。服务端
同时记录 `response_send_failed stage=header|body ... process_ms=...`，客户端失败日志
带 `timeout_ms=5000`，便于区分服务不可用与服务端处理超过等待窗口。
