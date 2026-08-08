# 动态 NPC：装备回收商人

phase: dynamic-npc-equipment-buyer

status: superseded by `2026-08-08-npc-equipment-sell-progress-stall.md`

## 触发与第一次偏离

此前动态 NPC 只有购买、修理、技能和副本等服务类型。后台可以配置商人模型，
但没有一个服务类型能让玩家把背包中的装备变现；若把它误接到普通商城的 `26/3`
或强化使用的 `29/*`，客户端会进入不同的模块／解析器，无法保证回到场景对话。

预期行为是：玩家触碰专门的动态 NPC，选择出售装备，看到自己背包中的装备列表，
确认一件后只移除该背包实例并得到铜钱。实际缺口位于动态 NPC 的 `npc_kind` 到
`26/1` 服务菜单映射；并非背包字段缺失，也不是客户端 UI 需要修补。

## 证据记录

request:

- wt_kind: `26`
- wt_subtype: `1`
- objects: 单一 `1/26/1`
- key_fields: 服务菜单点击为 `type=2,id=<private value>`；普通 NPC 对话为
  `type=1,id=<actorId>`。
- packet_log: 既有动态商店、修理、技能服务均使用 `builtin-npc-service`。

response:

- wt_kind: `26`
- wt_subtype: `1`
- fields: 首对象 `26/1 {hidebtn,dialog}`。出售成功后由下一次原生背包列表
  查询读取已提交的背包状态；不在该对话回调中附加背包变更对象。
- arrays: `dialog` 为 `ParseNPCDialogData` 的选项序列；每页最多五件装备和两个翻页项。
- strings: 选项显示中文回收价，完成文本显示实际获得铜钱。

ida_evidence:

- binary: `江湖OL.CBE`
- function: `task_hall_activate_selected_entry/0x010492B0`、
  `ParseNPCDialogData/0x010380E8`、`0x01033544`
- dispatch_case: action `1` 发送 `26/1 {type=2,id=value}`；
  `DispatchItemEvent(0x01039C28)` 在处理任意 kind-26 响应后清除该请求的
  等待状态。`7/7 type=2` 实际进入 `mmGame:sub_D04(0x0D04)` 的装备安装路径，
  不具备“删除背包装备行”的语义。
- parser_reads: 对话序列读取 `name/action/value/description`；物品更新读取实例
  `seq`、`itemId`、`count`。
- failure_branch: 在服务对话回调中发送 `17/1` 会把仅由背包模块消费的全量列表投递给
  错误回调；购买实现和 `mmGame:0x418C` 已将该错误路径排除。

runtime_evidence:

- trace_lines: `mock_npc_dialog ... service_action=...` 与
  `mock_npc_service action=equipment-sell...`。
- handled_source: `builtin-npc-dialog`、`builtin-npc-service`。
- queued_event: 现有服务端正常网络数据事件路径。
- client_effect: 客户端停留在 NPC 对话、背包选中实例被移除、回收列表重新生成。

negative_evidence:

- missing_or_bad_field: 用商品 ID 而不是背包 `seq` 会把同 ID 的多件装备混为一件，
  同时不能保留实例强化状态。
- observed_failure: 直接复用普通商城 `26/3` 或强化 `29/*` 没有客户端请求／响应证据，
  会跨入错误 UI 模块；本功能没有采用它们。

unknowns:

- name: 原版强化装备回收加价规则
  current_value: 无；当前按目录基础价值的 50% 向上取整。
  why_kept: 未发现原版资源或服务包证实强化等级应改变出售价；不能凭猜测增加收益。

## 修改点

- `VM_NET_MOCK_NPC_KIND_EQUIPMENT_BUYER=7` 进入动态 NPC 表、加载校验和后台下拉框。
- 对话选项的 `0xED`／`0xEE` 私有命名空间只接受已证实的 `26/1 type=2` 形状，且
  仅扩展原有私有服务操作范围。
- 出售前重新验证活跃角色、背包实例序号、目录装备属性和数量；只消费一件。
- `vm_net_mock_role_db_save("npc-equipment-sell")` 使用既有关系型角色事务，一次提交
  同时写入 `account_roles.money` 和 `account_role_backpack`。
- 成功响应只返回 `26/1`。背包在其自身 `17/1` 原生查询时从已提交数据库状态重建；
  不伪造商城或背包页面响应，也不把装备安装流当作删除流。

## 验证

- `php scripts/dynamic-npc-equipment-buyer-regression.php`：隔离账号中六件相同
  `item_id=1001` 但不同 `seq` 的装备第一页为 `5` 件、第二页为 `1` 件；出售 `seq=41`
  只删除该行，角色铜钱精确增加该行菜单展示的回收价，并返回
  仅返回 `26/1`；重复提交同一序号不再加钱或下发背包变更对象。
- `make -j2`：通过。
