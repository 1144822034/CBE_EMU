# 账号管理「给予物品」不到背包（2026-08-29）

## 触发条件

网页后台「账号管理」→「给予物品」：提示「物品给予成功」，但玩家背包无该物品
（离线发放后登录、或刷新角色明细后更明显）。

## 预期 / 实际

| | |
|---|---|
| 预期 | 按表单 `role` + `item` + `amount` 写入该角色 `account_role_backpack`，登录/重开背包可见 |
| 实际 | 内存背包已加，后台成功；离线 `release_if_idle` 丢掉 `account_state` 后 MySQL 仍是旧行 |
| 首次偏离 | `vm_mock_service_account_grant_role_item` 在 `add_backpack` 后只 `account_capture`，未 `role_db_save_role` |

## 根因

与 2026-08-03 非当前角色调 W 币同类：后台改库存依赖进程内 capture，权威表未更新。
该文档「已知风险」已点名给予物品路径。

加钱 / W 币 / 装备洗练已走 `vm_net_mock_role_db_save_role(reason, role->roleId)`；
给予物品漏了同一契约。

## 修改

`vm_mock_service_account_grant_role_item`：`add_backpack` 成功后调用
`vm_net_mock_role_db_save_role("admin-item-grant", role->roleId)`，失败则返回「物品给予持久化失败」（与洗练路径同：close 时 capture）。
成功后再 capture + close。

## 验证

- `make -j2`
- 离线账号：给予物品 → 登录进游戏开背包可见；MySQL `account_role_backpack` 有对应行
- 多角色：对非当前角色给予，刷新后台角色明细背包计数正确
- 背包已满仍报「角色背包已满」

## 已知边界

- 在线角色：后台不推送 7/7；需重开背包或重登才看见（与洗练后台路径一致）
- 打开管理会话会强制 MySQL 重载，未 flush 的在线脏库存可能被覆盖（同其它账号管理写路径）
