# 战斗击杀后的任务进度上报（WT 6/3）

## 现象

角色 `guest00023/10023` 接取任务 19 后击杀挑战怪 105。服务端已经记录
`mock_task_battle_progress ... progress=2/10 state=1`，随后收到一个未处理的
`WT 6/3` 单对象请求并在开发构建的 unhandled 断言退出：

```text
[error][network] unhandled wt=6/3 len=32 objects=1 first=1/6/3:23
Assertion failed: src/mock-server.c:44674
```

崩溃日志：
`tmp/mock-service-19090.teleport-progress.20260720-115752.stdout.log` 与对应
stderr。

## 客户端证据

- 二进制：`江湖OL.CBE`
- `UpdateTaskProgress`：`0x01047ACE`
- `alloc_outgoing_game_event(4, 0, 6, 3)`：`0x01047BEC`
- 任务响应分派：`0x0104726C`

`UpdateTaskProgress` 遍历客户端当前任务，更新两个本地进度槽，然后把
`taskinfo` 写成以下 tagged 流并发送 `1/6/3`：

1. `u32 taskId`
2. `u8 progress1`
3. `u8 progress2`

对应字节长度固定为 12：`00 04 + BE32`、`00 01 + u8`、
`00 01 + u8`。当两个进度均达到要求时，客户端另行调用
`SendTaskStateUpdate` 发送 `6/6`；因此 `6/3` 只负责进度上报，不能再次按
击杀数累计。

`0x0104726C` 没有响应 case 3，但 case 2 只读取 `result`，且客户端没有
`6/2` 请求发送点。这与 `6/3` 请求对应 `6/2 {result}` 确认的契约一致。

## 服务端契约

- 检测：单个 `major=1, kind=6, subtype=3` 对象；字段 `taskinfo` 必须为
  上述 12 字节 tagged 流。
- 身份：使用当前 service session 的 active role，不接受包内角色身份。
- 状态：读取 `account_role_tasks`；只允许已有的活动/完成任务和已知任务定义。
- 合并：客户端报告值按任务需求上限截断，再与数据库进度取最大值。这样重发
  不会重复累计，乱序旧包也不会导致进度倒退。
- 完成：两个进度达到要求时可把状态推进到 2；后续客户端 `6/6` 保持幂等。
- 响应：`1/6/2 { result: u8 }`，成功为 0。

## 实现与验证

实现位于 `src/mock-server.c`：

- `vm_net_mock_task_read_progress_blob`
- `vm_net_mock_build_task_response` 的 subtype 3 分支

验证：

- `make -j2` 通过。
- 使用与崩溃包相同的 32 字节 `WT 6/3` 形态回放任务 19、进度 `2/0`。
- 服务返回 23 字节 `WT 6/2 result=0`。
- 日志显示数据库权威值保持 `2/0 state=1`，没有二次累计：

```text
mock_task_progress_report task=19 role=10023 reported=2/0 authoritative=2/0 state=1 result=0
net_send connect=0 wt=6/3 len=32 source=builtin-task resp=23
```

- 回放服务 stderr 为空，进程没有断言退出。验证日志：
  `tmp/mock-service-19090.task-progress.20260720-122619.stdout.log`。

## 满进度时的 `6/3 + 6/6` 组合包

任务 19 达到 `10/10` 后，真实客户端把 `UpdateTaskProgress` 和紧接着的
`SendTaskStateUpdate` 一起刷新为 65 字节 WT 包：

```text
1/6/3 payload=23 { taskinfo = u32 taskId + u8 progress1 + u8 progress2 }
1/6/6 payload=28 { tasknum=1, taskid = tagged u32 }
```

旧处理器只允许单对象 `6/3`，因此该包落入：

```text
unhandled wt=6/3 len=65 first=1/6/3:23,1/6/6:28
Assertion failed: src/mock-server.c:44998
```

客户端证据：

- `江湖OL.CBE:UpdateTaskProgress(0x01047ACE)` 生成 `6/3`；进度满足要求时
  紧接着调用 `SendTaskStateUpdate(0x01046E64)`。
- `SendTaskStateUpdate` 生成 `6/6`，写入 `tasknum=1`，并把同一任务 ID
  作为 `taskid` tagged-u32 流。
- `net_handle_task_response_dispatch(0x0104726C)` 分别以 case 2 消费进度
  ACK、case 6 消费任务状态刷新。

服务端只接受这一种精确尾随形式：首对象必须是 payload 23 的合法进度流，
尾对象必须是 payload 28 的 `tasknum=1` 状态流，而且两者 task ID 必须一致，
不允许第三个对象。处理时按请求顺序复用单对象任务处理器，响应为：

```text
1/6/2 { result=0 }
1/6/6 { tasknum=1, taskstate=(taskId,state=2) }
```

回放结果：请求长度 `65`，响应长度 `63`，日志来源
`builtin-task`；MySQL 权威状态保持 `progress=10/0,state=2`，服务继续运行且
stderr 为 0。验证日志：`tmp/mock-service-19090.task-combo.*.stdout.log`。
