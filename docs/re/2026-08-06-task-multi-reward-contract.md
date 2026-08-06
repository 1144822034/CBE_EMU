# 任务多物品奖励与 Actor 选择器（2026-08-06）

## 目标

后台任务编辑允许配置多个奖励物品；场景动态 NPC 的 Actor 资源不再依赖难以查找的
下拉框，改为可搜索、带服务端 SVG 预览的选择器。

## 任务奖励契约

### 客户端证据

`JianghuOL.CBE:net_handle_task_response_dispatch(0x0104726C)` 的 `6/4` case 4
读取原始 `awardinfo` 时，顺序读取经验、铜钱、`u8 itemCount`，然后对每一项读取
`seq`、`itemId`、`count` 和 `ParseEquipAttributes` 公共属性流，并逐项进入增量入包路径。
因此 `awardinfo` 是专用的多行奖励流，而不是只能包含一件物品的 `17/1` 背包列表。

### 首次偏离与修复

旧服务端数据模型只保留 `reward_item_id/count/type` 一组字段，且 builder 固定写
`itemCount=1`。因此多项奖励无法表示；若把余项塞入不相关的 `7/7` 或背包全量对象，
会脱离 case 4 的原生回调所有权。

现在的权威存储为：

```text
server_tasks.reward_item_*          兼容首项 / task.dsh 回退
server_task_reward_items(order 0..7) 已保存任务的有序完整奖励
```

保存任务时在一个 MySQL 事务中更新 `server_tasks`、清空旧奖励行并写入全部新行。
提交时先将条件物品消耗和全部奖励添加投影到同一背包快照，确认容量、装备实例和
神仙壶/逍遥壶容器语义均成立后，再一次持久化角色。`6/4.awardinfo` 以实际背包序号和
逐项增量构建所有奖励行；容器项使用真实初始化储量。

约束：最多 8 项，物品 ID 不可重复，每项均须同时有 ID 和数量。装备及神仙壶、逍遥壶
是独立背包实例，每条奖励的数量必须为 1，不能伪装成一个序号上的堆叠。无奖励任务仍发送
`itemCount=0`。

## Actor 选择器契约

候选 Actor 仍来自服务端资源根目录，且仅展示
`vm_net_mock_dynamic_npc_actor_resource_is_supported()` 明确允许的资源。选择结果继续
提交原有 `actor_resource` 字段，不改变动态 NPC 保存或 WT 18/7 资源下载路径。预览通过
后台已有 `/admin-418yz6/actor-preview.svg?actor=...` 只读生成，缺失预览不会放宽保存
时的 Actor 资源校验。

## 验证边界

- 管理端：多行奖励和 Actor 选择器均是常规 HTML 表单字段，未引入客户端内存或协议补丁。
- 协议端：实际客户端验收应提交一个至少含两种普通物品及一件装备的任务，确认 case 4
  关闭进度条、三项均显示在背包，并复测背包满但任务材料释放空间的边界。
