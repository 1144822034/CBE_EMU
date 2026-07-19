# 已完成任务的目的文本请求（WT 6/12）

## 目标阶段

- 场景：任务列表 / 任务详情界面。
- 目标：查看“初来乍到”等任务时，客户端能够结束等待状态并显示任务目标文本。

## 运行时证据

- 问题请求：`WT 6/12`，单对象，包长 20。
- 请求字段：`id:u32`。
- 复现时任务 3001 已持久化为完成状态 3；客户端界面仍保留该任务行并继续请求目标文本。
- 旧服务端仅允许状态 1/2，因此 builder 返回 0，随后进入 `unhandled wt=6/12` 并触发断言，界面进度条无法消失。

## IDA 证据

- `task_request_detail_text(0x01047E0C)` 创建 `6/12` 请求并写入 `id`。
- `task_handle_destinfo_response(0x01047F0A)` 从响应中读取字符串字段 `text`，复制到任务详情缓冲区并刷新文本控件。
- 该响应解析器不读取或校验任务状态；任务状态不是 `6/12` 响应契约的一部分。

## 协议结论

- 请求：`WT 6/12 { id:u32 }`。
- 响应：`WT 6/12 { text:string }`。
- 对已知任务，`6/12` 是只读文本查询，不应被数据库中的当前任务状态阻断。
- 未知任务 ID 仍保持拒绝，不扩大协议匹配范围。

## 实现与验证

- `vm_net_mock_build_task_response` 对已知任务始终生成非空 `text`，不再要求任务状态必须为 1/2。
- `make -j2` 通过。
- `php tmp/task-detail-regression.php 19090 3001 12` 通过；完成态任务返回 50 字节响应。
- `php tmp/task-detail-combo-regression.php 19090`、`php tmp/xse-npc-dialog-regression.php 19090` 与 `php tmp/game-type-scope-regression.php 19090` 均通过。
- 服务日志记录 `mock_task action=destination task=3001 ... response_subtype=12`，stderr 为空。

