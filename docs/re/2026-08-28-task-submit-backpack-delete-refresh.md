# 任务提交后的背包行删除刷新

## 现象与真实运行证据

接取物流任务 `2000` 后，客户端已立即显示任务货物；提交同一任务后，服务端记录
`task-commit` 并已从持久化背包扣除货物，但客户端当前背包仍显示该行。

`bin/server_out.txt` 的最新真实客户端会话（角色 `10093`）显示：

```text
mock_task action=accept ... response_objects=6 ... request=6/11 response=6/11
mock_task_submit_iteminfo task=2000 role=10093 seqnum=1 iteminfo_len=7
mock_task action=commit ... result=1 ... request=6/4 response=6/4
```

前一条 `6/11` 的六对象回包包含新加入的 `7/15 + 7/11`，用户已确认接取货物立即
显示；后一条提交记录证明服务端已经用同一条背包序号生成 `6/4.seqnum/iteminfo` 并完成
数据库扣除。因当前画面未删除，首次可观察偏离是：任务回调内部的 iteminfo 没有更新
当前背包组件的可见行。当前 `bin/logs/net_trace.log` 与 `bin/logs/net_packets.log` 不存在，
故无法进一步判定该回调在该组件状态下为什么未应用；这一内部原因保持 `unresolved`。

## 已验证的客户端同步契约

- `JianghuOL.CBE:net_handle_task_response_dispatch(0x0104726C)` case 4 读取成功
  `6/4` 的 `seqnum` 与 tagged `(i16 sequence, u8 remaining)` `iteminfo` 流。
- `JianghuOL.CBE:0x01033544` 是独立的 `7/11` 按序号数量同步入口。现有药品、战斗、
  合成与回收路径均使用它；当数量为零时，客户端物品管理器执行该序号背包行的删除。
- 任务接取已经实测证明，任务回包后附带的通用背包对象会被客户端正常消费。因此提交
  成功回包可在原有 `25/5 + 6/4` 之后附带对应的 `7/11`，而不是发送只适用于背包界面
  回调的 `17/1` 全量列表，也不修改宿主或客户端内存。

## 计划修改

1. 保留 `6/4.seqnum/iteminfo`，不改变任务回调、持久化或奖励契约。
2. 仅在 `6/4 result=1` 且实际消耗物品时，按同一消耗清单追加每个
   `sequence -> remaining_count` 的 `7/11` 对象。数量是持久化后的绝对值，零值即删除。
3. 扩展确定性回归：断言接取创建的序号、`6/4` 的任务内嵌扣除以及随后的 `7/11`
   删除对象使用相同序号和零余量。

该改动仍只返回固件已知的 WT 对象，现有网络层保持不透明字节投递和原始 callback/context。

## 验证结果

2026-08-28 已执行：

- `mingw32-make -j2`：通过。
- `obj/server/task-delivery-item-consumption-regression.exe`：通过。夹具断言接取
  `7/15 + 7/11` 创建序号 `104`，同一序号随后编码为 `6/4.iteminfo` 的零余量，且
  成功构造独立 `7/11` 零数量对象。
- `obj/server/task-logistics-delivery-readiness-regression.exe`：通过，物流货物仍是
  任务可交付性的必要条件。
