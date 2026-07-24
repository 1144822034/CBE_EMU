# 任务提交奖励未刷新背包

日期：2026-07-24

状态：根因已确认并修复

## 触发条件

角色完成一个配置了 `reward_item_id` 和 `reward_item_count` 的任务，向交付 NPC
提交。服务端的正常请求链为：

```text
1/25/5(empty) + 1/6/4{taskid} -> 1/25/5 + 1/6/4{result=1,...}
```

提交成功后重新登录可以看到奖励物品已在 MySQL 的
`account_role_backpack` 中，但当前客户端会话的背包没有新增该物品。

## 证据与链路

1. `vm_net_mock_task_commit_reward` 先验证容量和任务状态，随后调用
   `vm_net_mock_role_add_backpack_item`，最后写回角色状态。当前任务表中 14 个
   带物品奖励的任务都使用不超过 10 的数量，因此不是容量、持久化或 16 位数量
   溢出问题。
2. `JianghuOL.CBE` 的
   `net_handle_task_response_dispatch(0x0104726C)` case 4 在读取成功结果后：
   - 以带类型/长度前缀的流读取 `awardinfo` 的 `u32 rewardExp`、`u32 rewardMoney`、
     `u8 itemCount`；
   - 对每个奖励条目读取带相同前缀的 `u16 seq`、`u32 itemId`、`u32 count`，再调用
     `ParseEquipAttributes` 和 `MoveBattleActorStep(item, itemId, count, seq, 0)`；
   - 该调用是客户端的增量入包/堆叠路径。
3. 现有服务端在提交成功分支只编码经验、铜钱和固定的
   `itemCount=0`。因此持久化完成后，客户端从未收到一个可供
   `MoveBattleActorStep` 消费的奖励条目。这里是持久化状态与当前 UI 第一次偏离的
   位置。

`awardinfo` 是原始序列流，不能按 WT string 编码；这与同一响应中的 `taskdes`
不同，后者必须保留内层字符串长度。

## 修复

提交奖励时由背包写入函数返回实际背包序号。`6/4.awardinfo` 将按客户端的原生格式
写入一条奖励：

```text
tagged-u32 rewardExp | tagged-u32 rewardMoney | tagged-u8 itemCount=1 |
tagged-i16 seq | tagged-u32 itemId | tagged-u32 incrementalCount |
tagged-i16 stackRuntime | tagged-i16 enhanceLevel | tagged-u8 attrCount=0
```

普通可堆叠物品的 `incrementalCount` 是任务配置的奖励数量；神仙壶/逍遥壶
（802/803）是独立的 HP/MP 容器，使用新建背包行的真实储量并以
`stackRuntime=1` 表示一个可见容器。无物品奖励的任务继续发送
`itemCount=0`。

不下发 `1/17/1` 背包全量列表，也不把任务提交伪装成其他物品操作：该列表只由
背包界面回调消费，而 `6/4` case 4 已有专用的原生奖励增量解析分支。

`vm_net_mock_task_commit_reward` 现在把实际写入/合并后的 `item_seq` 交给
`vm_net_mock_build_task_awardinfo`。后者只读取已持久化的背包行；普通堆叠项下发
本次增量，容器项下发其初始化后的真实储量。完整的带标签单条奖励需要 42 字节，
因此 `awardInfo` 缓冲区由 32 字节增至 64 字节。此前的 32 字节只容得下
“经验、铜钱、0 条目”，不能容纳一条真实奖励。

## 验证

扩展 `scripts/repeatable-npc-task-regression.php` 的隔离任务，使其奖励 3 个普通
物品（901）。脚本验证：

1. 提交后的 MySQL 背包行存在且数量正确；
2. `6/4.awardinfo` 的每个类型/长度前缀、经验、铜钱、条目数、序号、物品 ID、数量及
   零属性附加字段与该行和任务定义一致；
3. 无奖励任务仍使用零条目；
4. 编译通过，并以真实服务端回归请求复测 `25/5 + 6/4` 组合。

结果：

- `php -l scripts/repeatable-npc-task-regression.php` 通过；
- 使用独立 `19095` 服务执行
  `php scripts/repeatable-npc-task-regression.php run 19095` 通过；
- `make -j2` 通过，并已重启 `127.0.0.1:19090` 的正式 mock service。

隔离用账号、任务、动态 NPC 和背包行均由脚本的 `cleanup` 删除，未修改正式角色数据。
