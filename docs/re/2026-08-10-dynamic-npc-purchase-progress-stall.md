# 动态 NPC 商人购买后进度框不结束（2026-08-10）

## 触发与首个偏离

固定路径为：与防具商人对话 → 防具分类 → 选择购买一件商品。武器商人与
药品商人使用相同的 `1/26/1 {type=2,id=0xE9xxxxxx}` 购买操作和同一个服务端
分支，因此同样受影响。

运行日志中的防具商人 `actor=30000` 已给出完整证据：

```text
mock_npc_service action=armor-categories ... objects=1 resp=299
mock_npc_service action=shop-category ... objects=1 resp=339
mock_backpack_add item=5001 ... response=7/7-type1
mock_npc_service action=shop-buy ... result=1 ... objects=2 resp=438
```

前两步只有一个 `1/26/1` 对象且正常返回；购买在角色铜钱和关系型背包已写入后，
额外附带 `1/7/7 type=1`，这是第一个不同于可工作的菜单回调的响应对象。

## 客户端与契约

`江湖OL.CBE:task_hall_activate_selected_entry(0x010492B0)` 的选项 `action=1`
发出 `1/26/1 {type=2,id}` 并等待；`DispatchItemEvent(0x01039C28)` 对
`26/1` 调用 `ParseNPCDialogData(0x010380E8)`，随后结束该对话等待状态。

`mmGameMstarWqvga.cbm:sub_11CE(0x11CE)`/`sub_D04(0x0D04)` 确实可以解析
`7/7 type=1` 的 `iteminfo`，但这是物品管理器的独立回调语境，不能与上面的
`action=1` 对话完成包组合。把它拼在 `26/1` 后会在实际客户端保留进度框。

## 进一步取证：背包未刷新与真正根因

仅返回 `1/26/1` 能让进度框结束，但真实客户端在 NPC 对话关闭后**不会**自动发出
`1/17/1` 背包查询。因此购买已提交到 MySQL、重新打开背包却没有新物品；“等待下一次
原生查询”的假设与实际请求日志不符。购买后的正确背包契约仍是 `1/7/7 {type=1,
iteminfo=本次获得的一行}`。

根因不在物品行内容，而在远程客户端传输层已有但失效的后续对象分离器：
`vm_client_extract_item_followup` 把 WT 对象错误地从偏移 `4` 开始、并把对象长度读为
`start+3/+4`。真实 WT 包在 `packet[4]` 放对象数，对象从 `5` 开始，长度在
`start+4/+5`。所以分离器从未能识别复合响应，`26/1` 与 `7/7` 始终落在同一个
`action=1` 回调中。

## 修复

`vm_net_mock_build_npc_service_dialog_response` 的武器、防具、药品购买分支仍先提交
`vm_net_mock_role_add_backpack_item_to_role`，并在成功后按顺序生成：

```text
事件 A：1/26/1 { dialog=购买成功后的菜单 }
事件 B：1/7/7 { type=1, iteminfo=本次新增的一行 }
```

在线路上这两个对象仍是一个合法的 WT 复合响应；客户端
`vm_client_extract_item_followup` 已按真实 WT 布局修正后，仅对精确的
`26/1 + 7/7(type=1)` 两对象组合把第二个对象复制为下一条正常 scheduler 网络事件。
事件 A 先由 `ParseNPCDialogData` 清除对话等待，事件 B 再由 mmGame 物品管理器消费。
它不更改 CBE 内存、按钮或回调，也不重写对象字段；其他复合响应及已有的物品使用
`7/1 + 17/1` 配对保持原来的分离规则。

未改变的失败契约：背包满、铜钱不足、目录/服务上下文失效仍返回同一个 `26/1`
对话对象；购买前后的角色快照仍在不可能的后置条件下回滚并持久化。

## 回归范围

- `scripts/run-npc-purchase-equipment-swap-automation.ps1` 创建隔离
  `jh_online_autotest_*` schema 与独占服务端口。它经真实 `NPC 对话 → 服务菜单
  → 商品类别 → 购买` 请求分别覆盖武器、防具、药品，断言每次响应严格按
  `1/26/1 → 1/7/7(type=1)` 排列，同时核对 MySQL 中铜钱扣减与三条背包行新增。
- 武器、防具、药品的 `shop-buy`/旧 `weapon-buy` 共享该实现；它们以
  `serviceKind` 仅影响目录筛选，不影响购买完成响应。
- 真实客户端验收应分别购买武器、防具与药品，确认购买对话结束后背包立即出现新增行；
  客户端日志应记录 `remote_npc_purchase_backpack_followup ... separate-event`，证明两个
  parser 在各自的网络回调中执行。
