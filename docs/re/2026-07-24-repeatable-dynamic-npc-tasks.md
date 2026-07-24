# 动态 NPC 可重复接取任务

日期：2026-07-24

状态：已实现并以隔离服务回归验证

## 需求与边界

“可重复”是 NPC 与任务的绑定属性，不是全局任务属性。同一个任务可以仍由普通 NPC
保持一次性，也可以由另一个明确配置的动态 NPC 在完成后重新提供。因此设置保存在
`server_dynamic_npc_tasks.repeatable`：`0` 为默认的一次性任务，`1` 表示该绑定允许完成后
再次接取。

## 客户端协议链路

运行时和 IDA 已确认的客户端链路是：

1. 触碰 NPC 发送 `WT 26/1`；`ParseNPCDialogData(0x010380E8)` 解析 `dialog`。
2. 任务选项的 `action=4` 由
   `task_hall_activate_selected_entry(0x010492B0)` 消费，随后请求 `WT 6/10`，再请求
   `WT 6/11` 接取。
3. `6/11` 只包含任务 ID，不能携带“是哪个 NPC 的可重复配置”这一信息。

对话序列中的整数不是裸字节：`u8` 为 `00 01 xx`，`u32` 为 `00 04 xxxxxxxx`，字符串为
包含结尾 NUL 的大端长度串。隔离回归最初按裸值解析对话而错误报告没有 `action=4`；服务端
日志 `mock_npc_dialog ... task_offer=1 task_option_action=4` 和原始 `dialog` 字节首先证明服务端
已正确提供任务。回归解析器已按真实序列格式修正，不能把该测试解析错误当成服务端状态机问题。

## 根因与修复

原逻辑把 `account_role_tasks.task_state=3` 一律视为“永不再可用”。这正确实现了一次性任务，
但无法表达动态 NPC 绑定的重复语义。直接放宽 `6/11` 会更糟：客户端只传任务 ID，任意伪造
或过期请求都可能重开已完成任务。

修复位置和契约如下：

- 动态 NPC 加载、后台编辑和 MySQL 表保存 `repeatable`。无该列的既有库在服务启动时补列，
  默认值为 `0`。
- 构建 `26/1` 对话和 `6/14` 场景任务候选时，完成态仅在当前绑定 `repeatable=1` 时允许进入
  “可接取”分支。
- 服务会话记录这一次 `26/1` 实际提供的 `(roleId, scene, actorId, taskId, repeatable)`；下一个
  `6/11` 必须消费该上下文。完成态只能由同角色、同场景、可重复 NPC 对话产生的上下文重开。
- 重开时在 MySQL 事务内删除唯一的完成态行并插入状态 `1`、进度 `0/0`。若后续发放起始物品
  失败，恢复完成态，避免出现已删除完成记录但接取未成功的中间状态。

因此“完成后可重复”不改变其他 NPC 或其他任务，也不会接受裸 `6/11` 伪造包。

## 后台设置

后台 `动态 NPC 管理` 的新增、编辑表单在“可接取任务”下方新增“任务重复接取”：

- `完成后不可再次接取`（默认）
- `完成后允许再次接取`

未绑定任务时不能保存重复开关；服务端表单处理会拒绝该无效组合。

## 验证

执行：

```powershell
php -l scripts/repeatable-npc-task-regression.php
php scripts/repeatable-npc-task-regression.php setup
bin\jh-online-server.exe --mock-service-port=19095 --mock-admin-port=19096
php scripts/repeatable-npc-task-regression.php run 19095
php scripts/repeatable-npc-task-regression.php cleanup
```

回归在隔离账号、角色、动态 NPC 和自定义任务上验证：

1. 状态 `3` 的裸 `6/11` 被拒绝，数据库仍为状态 `3`。
2. 与 `repeatable=0` NPC 对话仍不会为完成态任务暴露 `action=4`。
3. 与 `repeatable=1` NPC 的 `26/1` 对话包含 `action=4` 任务选项。
4. `6/10 -> 6/11` 后数据库原子地变为状态 `1`、进度 `0/0`。

本次实际结果：`repeatable NPC task regression passed actor=59382 task=3999900001 state=3->1`。
