# 账号管理：非当前角色调整 W 币无反应

## 触发条件

账号下有多个角色时，在网页后台「账号管理」对**非当前角色**（`roleId != activeRoleId`）
点「调整 W 币」或「加钱」：提示可能成功，但刷新后余额不变；对带「当前角色」
徽章的那一行则正常。

## 预期 / 实际

| | |
|---|---|
| 预期 | 按表单 `role` 字段写入该角色的 `account_roles.wcoin` / `money` |
| 实际 | 内存已改目标角色，但 `vm_net_mock_role_db_save` 只把 `activeRoleId` 对应行 upsert 到 MySQL |
| 首次偏离 | `vm_net_mock_role_db_save_relational_ex` 在 `!full_snapshot` 时 `scoped_role_id = db->activeRoleId` |

离线账号管理结束后 `release_if_idle` 会丢掉进程内 `account_state`，下次打开再从
MySQL 加载，于是非当前角色的改动表现为「没反应」。

## 根因

后台按角色 ID 改余额，持久化却沿用游戏热路径的「只写当前角色」契约。权威行未更新。

## 修改

- `vm_net_mock_role_db_save_relational_ex` 增加 `scope_role_id`：非 0 时按该角色
  作用域写入，仍不改动 `account_role_state.active_role_id`。
- 新增 `vm_net_mock_role_db_save_role(reason, roleId)`。
- `account_add_role_wcoin` / `account_add_role_money` 改为按目标 `role->roleId` 保存。

## 验证

- `make -j2`
- 多角色账号：对非当前角色加 W 币 / 扣 W 币 / 加钱，刷新后数字正确；当前角色路径仍可用。

## 已知风险

给予物品路径仍主要依赖内存 capture，未走同一 scoped save；若出现同类「非当前角色赠物丢失」，应同样按 `roleId` 持久化。
