# 任务提交 `6/4 awardinfo` 截断调查（2026-08-05）

## 触发与证据

测试路径为在 `00蓬莱仙岛_02.sce` 与黄药师提交任务“救命之药”（任务 `4`）。服务端日志先记录：

```text
mock_task_reward task=4 role=10001 exp=100 money=40 item=50015 item_type=4 count=1
mock_task_awardinfo_invalid task=4 role=10001 item=50015 reward_seq=32 ... pos=64 cap=64 reason=serialize
```

客户端随后一直停留在“获取数据”进度条。第一个偏离不是客户端 UI，也不是任务条件：奖励已被提交，但服务端未能构造对应的 `1/6/4` 成功响应。

## 协议链路

`JianghuOL.CBE:net_handle_task_response_dispatch(0x0104726C)` 的 case `4` 在
`result=1` 时依次读取角色经验字段、空 `iteminfo`、原始 `awardinfo` 和任务描述，然后刷新任务 UI。
`awardinfo` 不是 `17/1` 背包列表；它是该回调专用的一次性奖励流。

服务端链路为：

1. `vm_net_mock_build_task_response` 接收 `1/6/4`；
2. `vm_net_mock_task_commit_reward` 消耗条件物品、写入奖励和任务状态；
3. `vm_net_mock_build_task_awardinfo` 序列化奖励；
4. 把该流放入同一个 `1/6/4` 对象，由客户端上述 case `4` 消费。

旧调用者只给步骤 3 分配了 64 字节。物品 `50015` 是装备；公共装备属性流含当前/上限、属性数和四个阶段强化属性，长度为 63 字节。完整奖励流的确定长度为：

```text
EXP tagged-u32       6
Money tagged-u32     6
row count tagged-u8  3
sequence tagged-i16  4
item id tagged-u32   6
count tagged-u32     6
common extra        63
----------------------
total               94 bytes
```

因此旧缓冲区会在 `pos=64` 处失败，响应生成返回 0；客户端未收到能结束当前回调的 `6/4` 对象。

## 修复

`mock_server_core.c` 现在定义公共属性流、单项更新、登录装备、附近玩家装备、战斗掉落和任务奖励的派生容量常量。`6/4 awardinfo` 使用精确的 94 字节常量，而不是历史 64 字节数组。

同一旧容量假设还存在于 `7/7` 单物品增删、装备登录、附近玩家装备查看和八行战斗掉落中；这些生产者改为对应的派生常量，避免相同的装备强化属性流在别的回调中再次被截断。

该修复没有改变任务条件、状态转换、奖励内容或客户端内存；它只让已确认的服务端响应契约拥有足够的序列化工作区。

## 回归

`tmp/task-awardinfo-capacity-regression.c` 使用真实 `equip.dsh` 条目 `50015` 验证：

- 旧 64 字节工作区必然拒绝该奖励流；
- 新任务奖励流严格为 94 字节；
- 单项 `7/7` 行严格为 82 字节；
- 八行战斗掉落流严格为 635 字节。

构建后运行该回归，再由人工按上述黄药师路径验收客户端 case `4` 能关闭进度条并刷新任务状态。
