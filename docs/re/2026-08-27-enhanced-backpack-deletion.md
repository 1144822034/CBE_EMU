# 已强化背包装备删除（2026-08-27）

## 触发与首个偏离

后台账号页的“删除物品”和用户中心的“丢弃”都提交精确的
`account_id + role_id + item_id + item_seq` 背包实例标识，随后汇入
`vm_mock_service_account_remove_role_backpack_item()`。普通物品可删除，但任何
`enhance_level > 0` 的背包装备均被回滚。

两个 HTTP 入口本身没有按强化等级拒绝请求：

- `POST /admin-418yz6/action`，`action=remove-role-backpack-item`；
- `POST /user/backpack/delete`，会话账号只能操作自己的背包。

首个错误状态在共享的完整快照保存前发生。服务已在内存中按精确序列清空目标行，
但 `vm_net_mock_role_db_save_relational()` 将“强化行数量或强化等级总和下降”一律视为
陈旧缓存覆盖，并回滚了该次操作。因此页面显示删除失败，数据库也保持原装备。

## 修复

`vm_mock_service_account_remove_role_backpack_item()` 在清空目标行前读取其强化等级，并只为
已验证的单个目标实例声明允许减少的强化行数和等级总和。完整快照保存仍会比较数据库与
投影快照：只有下降量不超过该精确实例的 `enhance_level` 时才继续提交；其余任何未声明的
强化状态下降仍会回滚。

这不是按请求字符串放宽保护，也没有改变客户端内存、WT 响应、账号归属或装备强化规则。
两条网页入口继续复用同一个精确实例删除服务，且非活动角色仍使用完整账号快照保存。

## 回归

`scripts/enhanced-backpack-deletion-regression.php` 要求服务运行在独立的
`jh_online_autotest_<hex>` 数据库上。它创建临时用户、两个 `+2/+3` 背包装备和临时后台
账号，依次执行：

1. 通过 `POST /user/backpack/delete` 丢弃 `+2` 装备；
2. 断言该实例消失而同角色的 `+3` 实例仍在；
3. 通过登录后的 `POST /admin-418yz6/action` 删除 `+3` 装备；
4. 断言目标装备消失、另一个角色的普通背包行保留，然后清理所有测试记录。

命令：

```text
CBE_AUTOMATION_MYSQL_PASSWORD=... php scripts/enhanced-backpack-deletion-regression.php <port> <jh_online_autotest_hex>
```

该脚本不控制桌面客户端、不接触用户账号，也拒绝非隔离数据库名。
