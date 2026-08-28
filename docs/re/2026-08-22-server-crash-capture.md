# 服务端原生崩溃取证

## 目的

服务端过去在原生访问违规时只保留 shell 的 `Segmentation fault`。最后一条
`net_send` 仅能证明 response builder 已经返回，不能区分账户状态回存、WT 合同审计或
socket 发送的哪一步发生了访问违规。

本次增加的是服务端宿主进程取证，不改变 WT 请求、响应字节、战斗状态、MySQL 事务或
客户端内存。

## 输出

`jh-online-server` 启动后输出：

```text
[info][mock-service] crash_capture enabled dir=logs/crashes
```

默认在服务工作目录下写入：

```text
logs/crashes/server-crash-YYYYMMDD-HHMMSS-<pid>.log
logs/crashes/server-crash-YYYYMMDD-HHMMSS-<pid>.dmp   # Windows
```

`CBE_MOCK_CRASH_DIR` 可指定已经存在的替代目录。

Windows `.log` 包含异常码、异常地址、宿主寄存器、调用栈地址，以及最近一次服务端
协议边界；`.dmp` 可用 WinDbg/GDB 结合同一构建的调试符号查看完整线程和栈。Linux
版 `.log` 包含 signal、地址和 `backtrace` 地址。

Linux 服务额外忽略 `SIGPIPE`，并让服务端 socket 发送使用 `MSG_NOSIGNAL`：已经断开
的游戏、后台、支付或 MySQL 对端会使发送函数返回失败，由现有调用方记录并关闭该连接，
而不会直接终止整个服务。`SIGTERM`、`SIGINT` 现在走正常的监听停止与 worker 排空，不再
生成崩溃报告；细节见 `2026-08-28-linux-graceful-shutdown.md`。`SIGHUP` 与 `SIGQUIT`
仍会在重新触发默认退出前写出终止报告。`SIGKILL` 和内核 OOM kill 无法由进程捕获；遇到
这两类停止必须一并保留 systemd journal 与内核日志。

## 协议关联边界

每个处理中的请求会在以下只读阶段更新“最近协议上下文”：

```text
request-dispatch
  -> response-built
  -> state-captured
  -> response-audited
  -> response-header-sent
  -> response-body-sent
```

其中 `response-built` 保留客户端 ID、WT kind/subtype、请求/响应长度和现有 handler
source。因而类似 `WT 4/12 -> builtin-battle-auto12-replay -> 4/6` 的异常可立即判断：

- 停在 `response-built`：检查 builder 返回后至账户状态 capture 的宿主路径；
- 停在 `state-captured`：检查 WT 合同审计；
- 停在 `response-audited`：检查 CBMS header/body 发送；
- 停在 `response-body-sent`：检查请求结束后的日志/worker 清理。

崩溃器保留原本的异常退出语义；它不会吞掉异常、重启服务、重发请求或改变协议行为。

## 下次复现的交付物

保留同一时间戳的 `.log`、Windows `.dmp`、`server_out.txt` 尾部，以及客户端侧的
崩溃 PC/LR（若客户端也退出）。只有该证据能确定问题属于服务端宿主、socket 边界或
客户端解析/特效路径，不能依据最后一条 `net_send` 直接改变战斗包。
