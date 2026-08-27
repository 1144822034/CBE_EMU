# 交付任务物品未从当前背包移除

日期：2026-08-27

状态：已实现；构建与回归暂被工作区既有的服务端拆分改造阻断

## 触发

用户在临安-南宣门提交“测试交付任务”后，背包中的任务物品仍然可见。

本地发布库 `jh_online_release_20260822` 的只读查询确认实际提交的任务为
`task_id=2000`（“测试交付物品”）：

```text
requirement_type1/2 = 0
given_item_id       = 26
given_item_count    = 1
```

这不是收集条件物品，而是任务接取阶段发放、交付阶段应回收的任务物品。

## 运行时证据与首次偏离

`bin/server_out.txt` 记录了同一角色 `10093` 的真实链路：

```text
6/11 accept task=2000 -> role save reason=backpack-add-item
6/4 commit task=2000 -> role save reason=task-commit
mock_task_reward task=2000 ... consumed=0
```

因此提交被正常识别并持久化，但现有 `vm_net_mock_task_commit_reward` 只汇总
`requirement_type == 1` 的两项收集条件。它遗漏了 `given_item_id/count`，导致
服务端根本没有扣除物品 26。

同一成功回包固定编码：

```text
seqnum = 0
iteminfo = empty
```

这又使当前客户端会话没有收到删除背包行的原生更新。这里是持久化状态与背包 UI
共同发生的首次偏离，不是 NPC 交付归属、客户端内存或任务状态机问题。

## 客户端协议证据

静态读取 `江湖OL.CBE` 的
`net_handle_task_response_dispatch(0x0104726C)` case 4 表明：

1. `0x010473D0` 读取 `seqnum` 并打开 `iteminfo` 原始流；
2. `0x010473F6` 和 `0x01047400` 每轮依次读取一个 `i16` 与一个 `u8`，再调用
   `0x010471B4` 应用到当前任务的背包项；
3. `0x01047434` 之后才开始读取 `awardinfo` 的经验、铜钱、条目数与奖励物品。

所以交付物品必须在同一个 `6/4` 对象中使用：

```text
seqnum = 消耗的不同背包行数
iteminfo = repeat seqnum:
  tagged-i16 item_seq
  tagged-u8  remaining_count
awardinfo = 原有的奖励增量流
```

不能改用 `17/1` 背包页面、额外回调或宿主侧 UI 操作；它们不属于 case 4 的原生
解析路径。

## 修复

`src/server/mock_server_scene_sync.c` 现在用一个唯一的消耗清单同时驱动：

- 条件物品与 `given_item` 的库存验证；
- 奖励容量投影；
- 真实背包行扣除与数据库保存；
- `6/4.seqnum/iteminfo` 的当前会话删除更新。

相同物品同时作为条件和给予物品时会合并为一次扣除，避免重复选择不同堆叠行。
每条 iteminfo 记录使用实际 `item_seq` 和扣除后的数量。由于客户端该字段读取
`u8` 余量，无法表示的余量会在任务状态写入前拒绝提交，不会静默截断或造成
客户端与数据库分叉。

## 验证

新增纯内存回归
`scripts/task-delivery-item-consumption-regression.c`，不连接服务、不启动客户端，
也不读写任何账号或数据库。它覆盖：

1. 任务给予物品进入统一消耗清单；
2. 物品从角色背包快照扣除；
3. `6/4.iteminfo` 严格编码为两个 tagged `(seq, remaining)` 删除项；
4. 大于一字节的剩余数量被拒绝且不会修改角色快照。

本次已实际执行 `make -j2` 及
`make task-delivery-item-consumption-regression`。两者均在进入本次任务代码前因
既有拆分改造失败：`src/server/mock-server.c` 与 `src/server/mock_server.h` 存在重复
类型/声明，且 `vm_mock_service_npc_context` 的声明与使用字段不一致。该故障与本次
任务提交文件无关，未修改这些并行改造中的文件。

拆分改造恢复可构建后，应执行：

```text
make -j2
make task-delivery-item-consumption-regression
obj/server/task-delivery-item-consumption-regression.exe
```

最终人工验收路径是对同一任务重新接取并提交：物品 26 应在提交成功时立即从背包
删除，重新登录后也不得恢复。
