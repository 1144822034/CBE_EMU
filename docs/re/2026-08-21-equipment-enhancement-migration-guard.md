# 装备强化迁移保护（2026-08-21）

## 根因

`account_role_equipment.enhance_level` 是后续版本新增的实例字段。旧部署在
非空装备表上执行 `ALTER TABLE ... ADD COLUMN ... DEFAULT 0` 时，MySQL 会把所有
历史行初始化为 `0`。之后角色加载/规范化的全量保存会把这个错误快照成功提交，
造成强化等级批量消失。普通强化事务失败不会产生这种结果：InnoDB 会回滚整笔
事务，而不是提交一批零值。

## 修改

- `src/server/mock_server_role.c` 的启动期 schema 检查现在在**非空旧表缺少
  `enhance_level`** 时拒绝自动 `ALTER TABLE`，保留原数据并记录
  `equipment_instance_schema_migration_blocked` 证据；只有空表/新表允许自动建列。
- 全量角色快照提交前比较数据库已有的非零强化实例数量和等级总和。投影快照低于
  现存值时回滚并记录 `equipment_enhancement_persist_blocked`，不执行删除/重写。
  `role-delete` 是明确删除角色及实例的业务操作，保留为唯一例外。
- `server/mysql/migrate_equipment_instance_state.sql` 增加同等 SQL guard。手工升级
  非空旧表会在 `ALTER TABLE` 前失败，要求停服、备份并使用能证明装备实例身份的
  专项迁移；不再按 `item_id` 猜测背包与装备槽的对应关系。

## 验证

```text
make -j2
equipment-enhancement-persistence-regression.exe
equipment-enhancement-state-guard-regression.exe
```

构建和两项隔离回归均通过。回归不连接或写入生产账号；当前本地
`jh_online_release` 也未被修改。

## 已知边界

历史已经被写成 `0` 的实例无法从当前 release 数据库恢复原等级。保护逻辑只阻止
未来再次静默清零；恢复旧库仍需从备份、binlog 或具备严格账号/角色/序号/物品实例
映射的历史数据生成人工审核清单。
