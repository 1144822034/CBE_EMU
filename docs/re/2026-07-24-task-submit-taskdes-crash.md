# 任务提交 `taskdes` 字段导致的客户端闪退

日期：2026-07-24

状态：根因已确认并修复

## 触发与运行时证据

测试角色接取任务 1 后，与交付 NPC“白展堂”对话并提交任务。服务端完整链路为：

```text
26/1 白展堂对话 -> 6/10 {task=1,state=3} -> 25/5 + 6/4 {taskid=1}
```

服务端日志确认最后的请求已被正确识别且奖励持久化成功：

```text
mock_task_reward task=1 role=10023 exp=40 money=10 item=20002 item_type=7 count=1
mock_task action=commit task=1 ... result=1 ... resp=224
```

客户端随后收到该 224 字节响应并在 `mem_copy(0x0104D3F0)` 的写入指令
`0x0104D3F0` 异常：目标地址为 `0x02100000`。返回地址 `LR=0x0104759D` 精确落在
`net_handle_task_response_dispatch(0x0104726C)` case 4 复制 `taskdes` 的调用之后，
而不是奖励、背包或场景刷新分支。

## 客户端契约

case 4 的成功路径先以 blob 读取 `iteminfo`、`awardinfo`；随后对 `taskdes` 调用：

1. 字符串数据访问器（对象方法 `+64`）；
2. 字符串长度访问器（对象方法 `+84`）；
3. `mem_copy` 到客户端固定的提交提示缓冲区。

`taskdes` 因此是 WT string/blob：字段值必须是 `be16(text_len) + text`。它不是
`iteminfo` 或 `awardinfo` 使用的裸序列流。

## 根因

服务端此前用 `vm_net_mock_put_object_raw` 写入 GBK 文本。字符串访问器遂将文本前两个
字节 `C8 CE`（“任”）解释为内层长度 `0xC8CE`，再把远超固定缓冲区的长度传给
`mem_copy`。崩溃是该字段契约首次被违反后的直接结果；任务状态、奖励物品和 `25/5`
等待结束对象均不是根因。

此前 224 字节的回归只验证对象名和顶层顺序，没有验证 `taskdes` 的内层类型/长度，因此
未覆盖这个解析分支。

## 修复

`vm_net_mock_build_task_response` 的提交成功分支改为
`vm_net_mock_put_object_string(..., "taskdes", ...)`。`iteminfo` 和 `awardinfo` 保持
原始流编码，不改变其各自的客户端 parser 契约。

## 验证

扩展隔离回归 `scripts/repeatable-npc-task-regression.php`：

1. 验证一次性/可重复 NPC 的接取边界；
2. 将无要求的隔离任务置为完成态，发送真实复合请求
   `25/5 + 6/4 {taskid}`；
3. 验证 `6/4.result=1`、`taskdes` 的内层大端长度和 GBK 正文“任务提交成功！”，
   以及 MySQL 任务状态变为 `3`。

这直接覆盖了本次崩溃的 `0x01047598 -> 0x0104D3F0` 客户端复制路径。
