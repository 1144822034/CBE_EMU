# 收集类任务提交后客户端未扣除物品

日期：2026-08-03

状态：已修复（`make -j2` 验证）

## 触发

NPC 收集物品任务（`requirement_type=1`，如梦境 `100004` 需 `841×5`）提交成功后：

- MySQL `account_role_backpack` 已无该物品（服务端扣过）
- 当前会话背包 UI 仍显示提交前数量

`lxh001/role=1` 于 22:58 提交 `100004` 后：`841 total=0`，奖励/徽章行正常。

## 根因

1. **客户端未收到扣除同步**：`6/4` case 4 只把 `awardinfo` 当增量入包流
   （`MoveBattleActorStep` 加奖励）；`iteminfo` 一直是空 raw。服务端
   `commit_reward` 已改内存/脏标记，但 live bag 缺
   `7/7 type=2` + `7/11.info`（与用药/强化扣玄晶同一契约）。
2. **跨堆叠扣除不完整**（相邻缺口）：`consume(role,id,seq=0,need)` 只动第一堆，
   堆数量 `< need` 时仍返回 true，会少扣。

首次偏离：持久化扣减成功之后、客户端解析 `6/4` 时没有消耗同步对象。

## 修改

- `vm_net_mock_role_consume_backpack_item_amount`：按堆耗尽并记录
  `(seq,itemId,remaining)`。
- `commit_reward` 改用上述扣除，并向调用方返回 sync 行。
- **扣除数量**：提交时对每个 type-1 需求物品，扣除背包中该物品的**全部持有量**
  （`backpack_item_total`），不是只扣 `requirement_count`；提交门槛仍要求
  持有量 ≥ `requirement_count`。
- 成功 `6/4` 后同包追加每行的 `7/7 type=2` + `7/11`（remaining 可为 0）。
- sync 编码失败只打警告，不毁掉已成功的提交结果。

## 验证

- `make -j2`
- 持有 `841×8`（需求 5，可分多堆）提交 `100004`：成功后背包 841 应全部为 0；
  日志含 `consumed_qty=8` 与 `mock_task_consume_inv_sync`
- 纯对话/击杀任务（无 type-1）提交响应对象数与改前一致
