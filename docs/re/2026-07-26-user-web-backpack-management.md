# 用户中心背包管理（2026-07-26）

## 目标

登录后的用户中心展示每个角色的背包，并允许账号所有者删除自己选中的一个背包实例。

## 数据与身份契约

- 角色背包的持久化行由 `account_role_backpack` 的
  `account_id + role_id + slot_index` 保存；客户端和服务端用于指向具体物品
  实例的是 `item_id + item_seq`。
- 页面每个删除按钮提交 `role_id`、`item_id` 与 `item_seq`。服务端不接受
  请求体中的账号字段，只从已验证的 `cbe_user` 会话读取 `accountId`。
- 删除处理器再次从该账号的当前角色快照中精确匹配三元组。不存在、序列已变化
  或数量为零的实例一律失败，不会退化成“删除同 ID 的第一个物品”。
- 删除针对整条背包实例：普通堆叠物品会删除该堆；装备、神仙壶/逍遥壶和修炼天书
  也按各自的 `item_seq` 删除。802/803 的 `item_count` 是 HP/MP 储量，页面将其
  显示为储量而非数量。

## 持久化链路

`POST /user/backpack/delete` → 会话账号校验 →
`vm_mock_service_account_remove_role_backpack_item` → 精确实例查找 →
`vm_net_mock_role_normalize_backpack` →
`vm_net_mock_role_db_save_relational(..., full_snapshot=true)` → MySQL 事务。

完整快照是必要的：普通 `vm_net_mock_role_db_save` 只写当前活动角色；用户中心可
操作账号下的任意角色，若只使用活动角色写入会造成非当前角色的删除没有落库。
该事务在 `account_role_backpack` 重新写入后同步 `account_role_training_books`，因此
删除最后一本修炼天书也会清理对应的附属记录。

HTTP 管理请求和游戏协议请求共用 `g_vm_mock_service_protocol_mutex`，所以删除过程
不会与同一服务进程内的角色状态切换并发交叉。成功后会捕获回对应账户状态，下一次
游戏请求读取的也是删除后的背包快照。

## 验证范围

1. 使用自己的会话访问用户中心，确认每个角色均显示物品名称、ID、序列、数量/储量、
   强化等级和删除按钮。
2. 删除普通堆叠物品、装备、802/803 和修炼天书各一项，刷新页面并重新登录游戏确认
   该实例消失，其余相同 ID/不同序列实例保留。
3. 篡改 `role_id`、`item_id` 或 `item_seq`，以及使用无会话或非 `POST` 请求，确认不会
   修改任何背包记录。
4. `make -j2` 后运行服务并检查用户中心 HTML 和删除重定向提示。
