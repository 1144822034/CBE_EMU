# 自动战斗末击的 MySQL 连接失效与结算合同

## 状态

- 日期：2026-08-07
- 阶段：已修复并完成隔离回归；等待用户长时间挂机复测空闲连接回收场景
- 范围：场景挂机或战斗中开启自动后的怪物战斗；不改变真实的八秒奖励冷却、决斗、
  组队回合屏障或客户端战斗模块。

## 固定证据

`bin/server_out.txt` 的异常 session `7` 依次记录：

```text
mock_hangup_battle_start ... battle=7 enemy=106 enemies=2 ... auto=1
mock_battle_auto_action ... session=7 turn=1 ... actionnum=3 ... next_tick=2715
mock_battle_auto_action ... session=7 turn=2 ... actionnum=3 ... next_tick=2746
mock_battle_auto_action ... session=7 turn=3 ... actionnum=2 ... next_tick=2767
monster_reward_cooldown_claim_failed ... error=MySQL socket send failed
mock_battle_terminal_close_deferred ... session=7 ... source=battle-operate-no-result-panel
mock_battle_auto_action ... session=7 turn=4 ... actionnum=2 ... next_tick=0 armed=0
mock_battle_terminal_close_deliver ... session=7 ... response=close
```

同一运行中，先前 session `5`、`6` 的最终击杀均有 `mock_battle_settle`、有效的
`4/7` 结算以及场景 `25/5` 收口。异常 session 在前三个 `4/6` 的动作数与间隔均
正常，第一次偏离是最终击杀奖励事务的首个 MySQL 发送失败；它没有 `mock_battle_settle`。

在异常前还出现过两次：

```text
monster_reward_cooldown_read_failed ... error=MySQL socket send failed
... retry_tick=... action=hold-scene-restart
```

之后下一次读查询可成功发起新战斗。这与「某 worker 保存的空闲 TCP 连接已经被 MySQL
端关闭，下一次发送才检测到」相符，而不是怪物 HP、`4/6` 动作队列或客户端动画时序错误。

## 客户端合同

按 `binary_name=mmBattleMstarWqvga.cbm` 选择 IDA 实例：

- `HandleServerBattleCmd` (`0x7BD0`) 把 `4/6` 交给
  `HandleBattleActionMsg` (`0x6EB0`)；动作列表是客户端逐项播放的队列。
- 胜利结果由 `4/7` 的 `HandleBattleSettleMsg` (`0x743C`) 进入原生结果面板。
- `4/11(type=0)` 清除自动标记，`4/9` 的推进又依赖该标记或死亡状态；它不是带奖励
  自动胜利的替代品。
- 原生结果面板经 `BattleScene_HandleInput` (`0x6258`) 的输入调用
  `BattleScene_ExitAndCleanup` (`0x60C8`)，发送 `WT 25/5` 回到场景。

因此「存储不可用」不是「成功但奖励为零」。当服务端把它标记成已结算，
`vm_net_mock_append_battle_status7_object` 会省略 `4/7`（零经验、零金钱的 `4/7`
本身也无客户端安全表示），随后自动终止路径投递 `4/11+4/9`。这正是用户看到的
自动停止、最后攻击动画循环且不再推进的第一个协议违约。

## MySQL 链路检查

服务端连接 worker 虽受 `g_vm_mock_service_protocol_mutex` 串行化业务状态，但
`src/mysql-client.c` 为每个 worker 保存一个 thread-local 长连接。现有
`vm_mysql_run_query()` 在 `COM_QUERY` 的第一次 `send()` 发现对端已关闭时立即失败，
不重连；后续轮次才可能在新连接上恢复。该发送失败发生在任何完整 MySQL 包交给服务端
之前时，MySQL 帧长度语义保证服务端不可能执行该 SQL，因此可以安全地重新连接并只重发
这一次未完整发送的请求。

不能对「完整 COM_QUERY 已发出后接收失败」或 `COMMIT` 的不确定结果做自动重放：这会造成
重复结算或错误的事务语义。本次修复只覆盖前一种可证明未执行的传输失败。

### 本机实例实测（根因确认）

2026-08-07 通过只读 PDO 查询本机 `jh_online` 得到：

```text
MySQL version               = 5.7.26-log
wait_timeout                = 120 s
interactive_timeout         = 120 s
net_read_timeout            = 30 s
net_write_timeout           = 60 s
max_connections             = 100
Max_used_connections        = 6
Connection_errors_internal  = 0
Connection_errors_max_connections = 0
Aborted_clients             = 774
```

因此根因已从假设收敛为：**该 MySQL 实例在连接空闲两分钟后主动回收非交互连接，而
服务端每个 worker 把连接保存在 thread-local `g_vm_mysql_socket` 中，既没有心跳，也没有
在下一条 SQL 前探活。** worker pool 会把后续连接分派给任意一个 worker；即便总体仍有
业务流量，某个 worker 也能空闲超过 120 秒。该 worker 下一次执行奖励 `START TRANSACTION`
或读冷却窗口时，才会在 `send()` 收到断链错误。

配置来源已经定位为正在运行的 phpStudy MySQL：
`D:\\phpstudy_pro\\Extensions\\MySQL5.7.26\\my.ini` 的 `[mysqld]` 中显式写有
`interactive_timeout=120`（第 20 行）和 `wait_timeout=120`（第 35 行）。

### 连接保活设计

用户不能调整 MySQL 配置，因此保活必须在服务端完成。不能用独立的心跳线程：
`g_vm_mysql_socket` 是 thread-local，独立线程只能新建并维持它自己的连接，无法阻止业务
worker 的连接超时。正确的归属是 `vm_mock_service_connection_worker_main()` 的空闲等待循环。

计划每 60 秒（小于已证实的 120 秒回收阈值）定时唤醒每一个空闲 worker；仅当该 worker
已有 MySQL 连接时，发送原生 `COM_PING`（命令字节 `0x0e`），而非 `COM_QUERY` 空字符串。
`COM_PING` 没有 SQL 解析、副作用或事务语义。成功时更新 MySQL 端的活动时间；失败时只关闭
该 worker 的本地失效 socket，并留下低频日志，下一笔业务请求仍经已有的安全重连路径建新连接。

心跳不持有 `g_vm_mock_service_protocol_mutex`：它不读取/写入账号、角色、战斗或协议全局状态，
只访问当前 worker 自己的 TLS socket；业务请求仍维持原有全局状态锁。

`Max_used_connections=6/100` 与两个 `Connection_errors_*` 为零，排除了连接数耗尽和
MySQL 内部资源拒绝；`Aborted_clients=774` 是实例层面已反复发生客户端连接异常终止的佐证，
但它是全局计数，不能单独归因给江湖服务端。CBE/CBM 不参与 MySQL 传输，故本小节没有
适用的 IDA parser；IDA 证据仍只用于上一节的客户端 `4/6` / `4/7` 结算合同。

## 修改点

1. 在 `src/mysql-client.c` 记录每次 MySQL 包是否已完整写入套接字。
2. `vm_mysql_run_query()` 仅在首个 `COM_QUERY` 未完整写出时关闭失效连接、重连并重发一次。
   完整写出、接收失败、SQL 错误和提交不确定性仍按失败处理，不做重试。
3. 使失败原因保留在日志中，以便若出现非「未完整发送」的持久化错误时继续取证，而不是
   将其伪装成零奖励结算。
4. 在 worker 的空闲等待边界发送低频 `COM_PING`；该保活不创建闲置连接，且失败不会重放
   已完整发送的业务 SQL。

## 验证边界

- 自动战斗跨越已被 MySQL 关闭的空闲 worker 连接时，首个查询应记录一次
  `mysql_transport_retry`，奖励 claim 仍成功，最终响应是带奖励 `4/7`，不出现同 session
  的 `battle-operate-no-result-panel` 或 `mock_battle_terminal_close_deliver`。
- 已完整发送后的接收失败不允许自动重发 SQL；请求失败必须保持可观察。
- 正常 SQL 错误、八秒冷却和无奖励的既有契约不改变。

## 本轮实现与验证

- `src/mysql-client.c`：`vm_mysql_send_packet()` 现在记录完整包写入边界；
  `vm_mysql_run_query()` 仅对未完整写出的首个 `COM_QUERY` 关闭失效连接、重连并重发一次，
  并记录 `mysql_transport_retry phase=com-query-incomplete`。已完整写出的请求、接收失败和
  `COMMIT` 仍直接失败，不会产生重复 SQL。
- 构建：`make -j2` 通过（2026-08-07）。
- 自动化：

  ```text
  powershell -NoProfile -ExecutionPolicy Bypass \
    -File scripts/run-shop-return-hangup-automation.ps1 \
    -Scenario hangup-auto-terminal-v1
  ```

  运行目录：`artifacts/automation/hangup-auto-terminal-v1-20260807T074023989Z-26532/`。
  它使用独立端口、独立 `jh_online_autotest_*` schema 与测试账号，完成后已清理。结果为
  `passed / native-4-7-reward-panel-received`；服务端依次记录三次自动 `4/6`、
  `mock_battle_settle ... reward_claimed=1` 和最终 `4/7`，没有
  `battle-operate-no-result-panel` 或 `mock_battle_terminal_close_deliver`。

该隔离回归验证了真实客户端的三怪自动终局仍走原生结算链。部署端已实测为 120 秒
`wait_timeout`；新日志会在用户原路径首次命中时保留其证据，而不会把它静默改写为“零奖励”。

### MySQL worker 保活隔离回归

新增服务端 transport 场景 `mysql-worker-keepalive-v1`：

```text
powershell -NoProfile -ExecutionPolicy Bypass \
  -File scripts/run-mysql-worker-keepalive-automation.ps1
```

它创建唯一的 `jh_online_autotest_*` schema 和私有资源副本，在私有端口启动唯一由该脚本
记录 PID 的服务端；随后发送一个格式正确的 `WT 1/1/12` 登录请求，以使真正处理该请求的
worker 走正常账号校验查询并取得 thread-local MySQL socket。该请求只是建立服务端传输夹具，
不替代客户端业务验证，也不写入用户的 `jh_online`。

最终运行目录为
`artifacts/automation/mysql-worker-keepalive-v1-20260807T083348569Z-47804/`：

- 初始可见连接 `4002`（启动线程）和 `4003`（登录 worker）；
- 65 秒后 worker `4003` 仍为同一 MySQL connection id，`Sleep=5`；
- 125 秒后（已跨越实例 120 秒 `wait_timeout`）仍为 `4003`，`Sleep=5`；
- `result.json` 记录 `passed`、两次计时器刷新和无 `mysql_keepalive_reset` / 重试。

启动迁移线程的 `4002` 没有后续业务归属，因此允许在本测试中自然过期；断言固定的是实际
处理登录查询、后续会接到战斗/账号事务的 worker `4003`。这验证了保活归属正确，不会通过
另起心跳线程错误地维持无关 socket。
