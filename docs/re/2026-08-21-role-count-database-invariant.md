# `role_count` 数据库不变量（2026-08-21）

## 根因结论

线上历史证据显示，948 个账号曾在同一秒被批量写成
`COUNT(account_roles)+1`。当时 general log、binlog 均关闭，数据库没有
trigger/event，因此具体旧服务、脚本或运维 SQL 仍是 `unresolved`；不能把某个
当前代码调用点冒充为已经证实的写入者。

可以确定的首个偏离是：把 `account_role_state.role_count` 当成可由缓存快照覆盖的
持久字段。`account_roles` 的行集合才是角色身份的权威来源。启动一次性迁移和 v3
保存路径修复只能约束本服务，不能阻止第二个连接直接执行批量 `INSERT`/`UPDATE`。

## 修改

- 启动阶段继续以 `COUNT(account_roles)` 校正已有偏差，并保留角色索引、活动角色和
  角色上限预检。
- 启动时安装并核验五个 InnoDB trigger：状态表 `BEFORE INSERT/UPDATE` 强制使用
  权威计数，角色表 `AFTER INSERT/DELETE` 维护计数，角色归属变更时 `AFTER UPDATE`
  同时维护源、目标账号。
- trigger 名称已存在但事件、时机、对象表或动作不匹配时 fail closed，不会静默接受
  被替换的 trigger。
- 状态表直接写入的错误尝试记录到 `account_role_count_write_audit`，包括账号、尝试
  值、权威值、连接 ID、数据库用户和时间。应用自身把正确权威值写入时不会制造审计噪声。
- 即使 `role-count-authority-v3` marker 已存在，启动仍执行一致性巡检；发现后续污染
  时修正并记录 `action=runtime-reconciled`，不再用 marker 跳过检查。

## 回归证据

`scripts/run-role-count-authority-migration-regression.ps1` 只创建并删除唯一命名的
`cbe_auto_*` schema，覆盖：

- v2 marker 存在时的历史 `3 -> 2`、`1 -> 0` 校正和重复启动；
- 单账号错误 UPDATE、两账号批量 UPDATE、错误 INSERT 均被 trigger 改回权威值，并
  为每次错误尝试生成审计行；
- `account_roles` 插入和删除自动维护状态计数；
- 旧会话缓存保存路径仍会修正并失效缓存；
- 非连续角色索引导致迁移原子失败，marker 不被写入。

2026-08-21 在本机 MySQL 5.7.26 的隔离 schema 中通过。运行输出确认五个 trigger
均创建并通过定义核验，迁移校正 2 个账号，之后 marker 已存在的再次运行仍完成
巡检。具体旧批量写入者仍需线上开启审计或 binlog 后，通过
`account_role_count_write_audit` 的 `connection_id`、`database_user` 和时间窗口
继续定位。

