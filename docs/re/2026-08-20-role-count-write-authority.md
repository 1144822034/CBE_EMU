# `account_role_state.role_count` 写入权威修正

Date: 2026-08-20

Status: implemented-build-validated; isolated-DB runtime regression pending automation credentials

## 触发与首个错误状态

`account_role_state.role_count` 是 `account_roles` 的缓存聚合，不是角色运行时
快照字段。旧启动迁移 `role-count-authority-v2` 曾按 `COUNT(account_roles)` 修复
历史偏差，但 `vm_net_mock_role_db_save_relational()` 仍会在每次背包、装备、位置、
选角、离线经验或管理端角色保存时，将
`g_vm_net_mock_role_db.roleCount` 原样写回状态表。

因此，当角色迁移、后台管理或其他关系表操作已改变主表，而既有会话仍保存旧角色
列表时，下一次无关的 active-role 保存会重新污染 `role_count`。首个错误状态是把
派生聚合当成可由内存缓存覆盖的持久字段；一次性启动迁移无法阻止后续写回。

## 写入审计

当前写入点只有三类：

1. 启动迁移 `vm_mock_service_role_count_authority_prepare_and_migrate()`：以
   `COUNT(account_roles)` 修复历史数据。
2. `vm_net_mock_role_db_save_relational()`：角色创建、删除和所有普通角色状态保存。
3. 账号中心角色迁移：在同一事务内按锁定的源/目标 `account_roles` 行数计算两端数量。

其中第 2 项原先违反了聚合权威边界。

本次发布使用新的 `role-count-authority-v3` 标记。不能复用 v2 标记：已部署 v2 的
数据库曾在后续普通保存中被旧逻辑重新污染；v3 会在升级启动时再以关系表计数完整校正
一次，之后由新的保存契约阻止回写。

仓库级检索还命中了若干 `scripts/*-regression.*` 中的
`INSERT ... role_count=...`。这些语句仅用于创建隔离测试夹具，不会连接生产或用户
数据库，且其数值与同一夹具插入的 `account_roles` 行数一致；它们不是运行时写入路径。

## 修复契约

`vm_net_mock_role_db_save_relational()` 现在遵循以下规则：

1. 非全量保存开始事务后，先读取 `COUNT(account_roles)`；若与内存缓存不一致，仅把
   `account_role_state.role_count` 修正为该计数，提交后使当前账号缓存失效，并拒绝旧
   快照写入。
2. 全量创建/删除完成 `account_roles` 更新后，再重新读取主表计数；只有它与当前完整
   内存快照一致才提交，并使用该数据库计数更新状态表。
3. 任何路径均不再把 `g_vm_net_mock_role_db.roleCount` 直接拼入 `role_count` 更新。

这不会回退角色数据、不会猜测角色数量，也不会吞掉错误状态。缓存不一致时，下一次
请求会走正常关系 loader 重新获得主表的完整角色列表。

## 回归

`scripts/role-count-authority-migration-regression.c` 会先写入历史 v2 标记，再验证 v3
迁移。它另有新增场景：先加载两角色缓存，
再模拟关系表新增第三角色，然后执行普通 active-role 保存。断言保存返回失败、状态表
计数修正为 3、缓存被失效，随后关系 loader 能重新读取三条角色主行。

2026-08-20 验证：`make -j2` 及该回归程序的独立编译均通过。运行隔离数据库场景需要
`CBE_AUTOMATION_MYSQL_PASSWORD`；当前执行环境没有配置该变量，因此没有改用
`jh_online_release` 或任何用户数据库进行替代测试。
