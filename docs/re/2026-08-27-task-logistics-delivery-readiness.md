# 物流交付任务的可提交条件

Date: 2026-08-27

Status: validated

## 1. 当前卡点

- 可见现象：后台任务编辑器只把任务目标展示为“收集物品”和“击败怪物”，没有说明
  已有的“接取物品后交付”的配置方式；角色即使丢失接取时给的物品，也可能在交付 NPC
  处先收到可提交状态。
- 触发方式：创建一个两条 `requirement_type` 都为 `0`、设置 `given_item_id/count`，且
  发布者和交付者不同的任务；接取后丢弃该物品，再与交付 NPC 交谈。
- 本轮最小目标：把该流程明确为“物流交付任务”，并使状态 `1 -> 2` 同时要求两个原有
  进度槽完成和交付物品仍在背包中。

## 2. 运行时证据

- `docs/re/2026-08-27-task-delivery-item-consumption.md` 保留了任务 `2000` 的真实链路：
  `6/11` 接取时发放物品 `26`，`6/4` 提交应删除该背包物品。
- 该文档记录的首次偏离是旧提交路径只消费 `requirement_type == 1`，遗漏
  `given_item`；当前 `vm_net_mock_task_collect_consumed_items()` 已将给予物品纳入统一
  消耗清单和 `6/4.iteminfo`。
- 当前工作区没有可用的 `bin/logs/net_trace.log` 或 `bin/logs/net_packets.log`，本轮不把
  历史现象伪称为新的运行时采样。

## 3. IDA 目标

| binary | function/address | reason | findings |
| --- | --- | --- | --- |
| `江湖OL.CBE` | `net_handle_task_response_dispatch` `0x0104726C` | 任务状态与提交响应分派 | 既有取证确认 `6/6` 是状态 `2` 通知，`6/4` case 4 读取 `seqnum/iteminfo` 后更新背包。 |
| `江湖OL.CBE` | `SendTaskStateUpdate` `0x01046E64` | 状态 `1 -> 2` 的客户端更新 | 既有服务端 trace 注释和任务文档确认它消费 `6/6`。 |

当前会话没有可调用的 IDA 实例工具；以上地址来自已签入的历史 IDA 取证记录，且本轮不
改变 WT 对象结构或新增 client parser 分支。

## 4. 调用链 / 业务流程

1. `6/11` 接取任务，`vm_net_mock_task_grant_accept_item()` 将 `given_item` 写入角色背包。
2. 角色与定义中 `receiver` 指定的 NPC 交谈；
   `vm_net_mock_build_npc_dialog_response()` 判定该 NPC 是交付者。
3. 角色的两个已持久化进度槽达到阈值、且统一消耗清单中的物品都仍在背包时，服务端保存
   状态 `2`，并在同一 NPC 对话响应中追加已有 `6/6`。
4. 客户端依据该状态显示 `action=4` 的提交入口；随后 `6/4` 使用已有的
   `iteminfo` 删除货物、持久化状态 `3` 并下发奖励。

## 5. 结构体 / 状态字段笔记

- owner: `vm_net_mock_task_definition`
- fields: `requirementType1/2`、`requirementCount1/2`、`givenItemId`、
  `givenItemCount`、`giver`、`receiver`
- read site: `vm_net_mock_build_npc_dialog_response()` 的三个 state `1 -> 2` 交付分支。
- current meaning: `requirement_type` 仅支持 `0/1/2`；`given_item` 是接取时发放、交付时
  回收的任务货物，不是新的客户端 condition enum。
- confidence: high；字段来自 `task.dsh`、`server_tasks`、现有接取/提交代码及上述 client
  parser 取证。

## 6. 请求 / 响应契约

### Request

- NPC 交谈：现有 `26/1` 对话请求。
- 提交：客户端只在已显示 action=4 后发送 `1/6/4 {taskid}`。

### Response

- 状态就绪：现有 `26/1 + 6/6 {state=2}`；不引入 `requirement_type=3` 或新 object。
- 提交成功：现有 `6/4 {result=1, seqnum, iteminfo, awardinfo, taskdes}`；`iteminfo`
  用真实背包 `seq` 和剩余数量删除物流货物。

## 7. 成功路径与失败路径

### Success path

- 两个目标槽位为 `0`，接取物品非零；角色持有全部货物并与正确交付 NPC 交谈，得到
  `6/6 state=2` 和提交入口；`6/4` 删除货物并完成任务。

### Failure path

- 货物被丢弃、消耗或数量不足：任务保持 state `1`，不生成 `6/6 state=2` 或提交入口；
  背包和任务持久化状态不变。

## 8. Negative Evidence

- 不采用 `requirement_type=3`：后台保存校验和客户端协议都仅支持 `0/1/2`，伪造新枚举
  会让客户端 parser 走未取证分支。
- 不在宿主侧改任务或 UI 状态：完成仍由现有 `26/1`、`6/6`、`6/4` 网络回调自然推进。

## 9. Unknowns / Hypotheses

- unknown: 原始官方任务是否允许物流货物与普通收集目标叠加。
  - current guess: 现有统一消耗清单已明确合并相同 item id，因此安全支持组合；本轮回归
    同时覆盖纯物流和既有收集/击杀路径不受影响。
  - why it matters: 不能把物流处理写成只针对 task 2000 的特判。
  - next probe: 通过纯内存回归检查合并的库存前置条件。

## 10. 本轮实现计划

- 计划改动：提取交付就绪谓词，在三个 state `1 -> 2` 分支中统一调用；任务后台增加
  物流配置说明和字段标签。
- 目标文件：`src/server/mock_server_scene_sync.c`、`src/web_admin_server.c`、新的确定性
  回归脚本和 Makefile 入口。
- 为什么这次只改这一小块：复用已验证的接取、NPC 归属、`6/6` 和 `6/4` 契约，只补齐
  货物持有的业务前置条件与配置可见性。

## 11. 验证清单

- [x] 未持有物流货物时不进入 state `2`
- [x] 持有全部物流货物时允许 state `2`
- [x] 普通无目标对话任务与击败怪物任务保持原有判断
- [x] 不新增客户端状态写入或 WT enum
- [x] `make -j2` 与纯内存回归通过

## 12. 实现与验证结果

- `vm_net_mock_task_delivery_is_ready()` 统一检查已持久化的两个进度槽和既有的
  `given_item`/收集物品消耗清单；三个 NPC 对话的 state `1 -> 2` 分支均改用它。
- 后台任务页明确“物流交付任务”的配置：目标列表保持为空，设置接取给予物品、数量和
  定义中的交付 NPC。没有新增 `requirement_type=3`。
- `mingw32-make -j2` 通过。
- `task-logistics-delivery-readiness-regression` 通过：无货物、货物不足、有完整货物和
  既有击败目标门槛均符合预期。
- `task-delivery-item-consumption-regression` 通过：任务提交仍以客户端 case-4 的
  `seqnum/iteminfo` 更新背包；边界用例以 `item.dsh` 的实际堆叠上限验证，避免把会被
  背包规范化拆分的非客户端可见行当成协议状态。
