# Linux `SIGPIPE` 导致服务静默停止（2026-08-25）

## 触发与第一处偏离

线上最后的服务日志先出现多个 worker 的：

```text
dropped malformed request worker=<1..4> ... queue_wait_ms=5276..5387
```

随后一个已正常解析的 `WT 18/5` 记录了 `mock_update_version` 和
`net_send ... source=builtin-update-version-catalog resp=23`，但没有后续
`response_send_failed`、崩溃报告或正常退出记录。

`net_send` 是响应 builder 的日志，不表示 CBMS header/body 已写入 socket。四个
worker 都已在队列中等待超过五秒，因此请求方可能已经在服务端实际发送前断开。

## 根因

Linux 崩溃捕获器原先只安装 `SIGSEGV`、`SIGABRT`、`SIGFPE`、`SIGILL` 和 `SIGBUS`
处理器；`SIGPIPE` 保持默认动作。`vm_mock_service_send_all()` 则以 `flags=0` 调用
`send()`。当延迟响应写入已断开的短连接时，Linux 会先向整个进程投递默认致死的
`SIGPIPE`，所以调用方来不及记录 `response_send_failed`，崩溃捕获器也没有报告。

MySQL 发送路径同样使用普通 `send()`，因此只在游戏 socket 层增加局部判断不足以保护
整个服务进程。

## 修复

1. `vm_server_install_crash_reporter()` 在 Linux 上全进程忽略 `SIGPIPE`；所有 socket
   发送（包括 MySQL）改为常规 `EPIPE`/失败返回，而不是终止服务。
2. 服务端 CBMS/HTTP 发送额外传递 `MSG_NOSIGNAL`，即使该发送函数在未安装全局信号
   策略的环境复用，也不会重新引入退出路径。
3. `SIGTERM`、`SIGINT`、`SIGHUP` 与 `SIGQUIT` 现在与致命信号一样先写出 Linux
   终止报告，再恢复默认退出动作。`SIGKILL`、OOM kill 仍不可捕获，必须查
   `journalctl`/内核日志。

## 验证边界

Linux 目标上的定向回归应关闭一端 `socketpair` 后调用实际
`vm_mock_service_send_all()`：预期函数返回失败、测试进程仍继续运行，且
`sigaction(SIGPIPE)` 显示 `SIG_IGN`。该测试不启动服务、不连接 MySQL，也不修改
游戏或账号数据。
