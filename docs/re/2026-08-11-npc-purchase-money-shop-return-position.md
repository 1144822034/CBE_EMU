# NPC 购买金额与商城返回坐标（2026-08-11）

## 触发步骤

1. 与武器、防具或药品 NPC 对话并购买一件物品；
2. 打开背包，物品行已经增加，但显示的铜钱仍是购买前数值；
3. 在任意非出生点位置打开商城并返回场景，角色被放回场景出生点。

## 客户端与服务端链路

### NPC 购买

请求是 NPC 服务对话的 `WT 1/26/1 { type=2,id=<商品服务值> }`。服务端的
`vm_net_mock_build_npc_service_dialog_response()` 先在权威角色状态中扣除
`role->money`，再由 `vm_net_mock_role_add_backpack_item_to_role()` 提交物品变更。

修复前响应仅含：

```text
1/26/1 { dialog=<购买成功> }
1/7/7  { type=1,...<新增背包行> }
```

`1/26/1` 只由 NPC 对话解析器消费，`1/7/7` 只更新物品管理器。因此角色的铜钱
字段没有后续同步，背包重新打开也只得到 `17/1 iteminfo` 和 `7/42`，仍然不会更新钱币。

IDA 实例按 `binary_name=江湖OL.CBE` 选择。`net_business_response_dispatch`
(`0x01012E4C`) 将 kind `10` 分给
`net_handle_role_login_gift_glamour` (`0x010114FC`)；该函数的 subtype `26` 分支读取
`name` 和 `money`，并把 `money` 写入客户端的三份角色状态。故购买成功的完整契约应为：

```text
1/26/1  { dialog=<购买成功> }
1/10/26 { result=1,type=1,npcnum=0,name=<角色配偶名>,money=<扣款后的铜钱> }
1/7/7   { type=1,...<新增背包行> }
```

`1/10/26` 使用项目既有 `vm_net_mock_append_type1_object()` 构造，未伪造客户端状态；
它从已提交的权威 `role->money` 读取金额。该对象只加入 NPC **购买成功**路径，购买失败、
技能学习、对话和背包查询的协议不变。

### 商城返回

商城打开后，客户端会发出场景运行时的 resource 或 task-subset follow-up。服务端凭该商城
请求建立的 session 标记进入 `shopReturnReload` 路径，并发送带 `posinfo` 的 `1/30/2`。

IDA `scene_handle_change_result_scene_pos` (`0x01039770`) 证明：当 `result==1` 且存在
`posinfo` 时，客户端将 blob 内的两个 `i16` 直接交给 scene-controller 重新创建场景；
无 `posinfo` 时只执行下载状态重置。因此 `posinfo` 的坐标并非提示或推荐出生点，而是新的
场景位置权威输入。

首次偏离在服务端：两个商城返回 builder 都将
`vm_net_mock_scene_spawn_x/y()` 填入该 `posinfo`。这与返回前角色位置无关，导致客户端正确地
把角色重入到出生点。

## 修复

- 新增 `vm_net_mock_get_shop_return_persisted_position()`：仅当活动角色的已持久化 scene 与
  返回 scene **精确相同**且 `x/y` 都有效时，返回其位置；不回退到默认/出生坐标。
- resource repeat 和 task-subset 两条商城返回分支都以该位置构造原生 `1/30/2 posinfo`。
  不能证明同场景持久化位置时拒绝该错误响应并记录
  `mock_shop_return_reenter_rejected`，保留商城返回标记以便取证，而不是发送会重置位置的包。
- 记录 `mock_shop_return_reenter_pos ... source=persisted-role`，用于人工回归确认响应位置。

## 验证边界

已完成静态协议核对和构建验证。待人工回归：

1. 在出生点之外购买物品，立刻打开背包，确认铜钱等于扣款后值；
2. 在场景任意非出生点打开商城、返回，确认角色落在返回前同一网格；
3. 分别覆盖 NPC 武器、防具、药品购买，以及商城返回后的挂机/战斗入口。

预期服务端日志分别含 `inventory_sync=role-wallet-10/26+item-manager-7/7` 和
`mock_shop_return_reenter_pos ... source=persisted-role`。若出现
`mock_shop_return_reenter_rejected`，应保留该日志并调查持久化场景/位置为何不一致；不得以
出生点坐标兜底。
