# Linux 优雅停服与已接收请求排空

## 根因与目标

此前 `jh-online-server` 将 `SIGINT` 与 `SIGTERM` 交给崩溃取证处理器：写出
报告后恢复默认信号动作并重新触发。这会在任意 worker 正在进行 MySQL 事务、构造
响应或写回客户端时终止整个进程；监听循环也没有退出和 worker 回收路径。

本次改动把 `SIGINT`（终端 Ctrl+C）与 `SIGTERM`（例如 `systemctl stop`）定义为
**一次正常停服请求**，而不是业务、协议或客户端状态变更。目标是让已经进入服务端
协议队列的完整 CBMS 请求沿原有 handler、MySQL 和 CBMR 路径完成一次，再清理宿主
资源。

## 生命周期与边界

1. `server_main.c` 的 Linux 信号处理器只写入 `sig_atomic_t` 停服标志和信号号；不
   写日志、不发包、不触碰 MySQL 或账号状态。第二次 `SIGINT/SIGTERM` 是操作者明确的
   强制退出，会放弃本文件的排空保证。
2. 监听线程在创建 game/admin workers 前临时屏蔽 `SIGINT/SIGTERM`，使 workers 继承
   该 mask；创建完成后仅监听线程恢复 mask。因此信号不会打断任一 worker 的 MySQL
   收发、事务或 CBMR 写入。被 `select()` 打断后监听线程检查停服标志。
3. 监听线程停止接受新的 game/admin 连接，并关闭尚未组成完整 CBMS 帧的 ingress
   sockets。此类连接还没有经过 request detector、handler 或持久化边界，不会产生业务
   写入。
4. worker pool 的正常 `drain` 不再清空已入队 jobs 或关闭它们的 sockets。已完整入队的
   game/admin 请求按原有顺序运行，分别由既有 handler 负责事务、响应和失败语义；worker
   退出前关闭自己的 thread-local MySQL socket，主线程最后关闭启动阶段 MySQL socket。
5. 服务只记录 `shutdown_requested`、每个 pool 的 `shutdown_drain` 和
   `shutdown_complete`。它不伪造客户端 disconnect、不会重投回调、不会改变 WT/CBMR
   字节，也不会把场景、战斗等内存临时状态强写为持久化状态。

角色的 movement timeline 已由其现有权威写入函数在每次有效更新中提交。不能在停服时
遍历并保存整份 account cache：其中包含场景加载、战斗和回包追踪等协议临时状态；将它们
写入数据库会绕过原有的 client callback/请求合同。

## 文件型更新状态

Linux 的 `server_update_catalog.tsv` 和 `server_update_delivery.tsv` 现在在临时文件
`fclose()` 成功后直接 `rename(temp, path)`。同一文件系统上的 POSIX `rename` 会原子
替换目标，移除了此前 `remove(path)` 与 `rename()` 之间 Ctrl+C 可能留下目标文件缺失的
窗口。该修复针对进程停止；磁盘或机器掉电的持久化等级仍取决于文件系统，若需该保证
还必须为临时文件和目录执行 `fsync`。

## 回归

Linux 上的定向、无数据库回归：

```text
make -j2 linux-graceful-shutdown-regression
./obj/linux-server/linux-graceful-shutdown-regression
```

场景 ID：`linux-graceful-shutdown-v1`。它不启动监听器、不绑定端口、不连接 MySQL，使用
`socketpair` 注入一个已经完整形成的 CBMS PING transport frame。步骤是：worker 继承
屏蔽信号的 mask；向该 worker 定向发送 `SIGINT` 证明其不会被中断；在监听线程上下文
发送一次 `SIGTERM`；调用正常 drain。断言是 SIGTERM 仅成为停服请求，且已入队 PING
仍返回原始 CBMR 空响应后 worker 才被 join。该测试不构造 WT 业务包、不改响应字节，也
不触及账户或数据库。

生产停服应使用 `systemctl stop`（SIGTERM），并让 `TimeoutStopSec` 大于实测最长请求和
外部支付等待时间。`SIGKILL`、OOM kill、第二次 Ctrl+C 或 systemd 超时后的强制杀进程
都不具备排空保证；必须保留 journal 与服务日志，并按 MySQL 提交记录核对最后一个请求。
