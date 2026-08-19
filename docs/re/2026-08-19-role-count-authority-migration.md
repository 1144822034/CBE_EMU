# 角色数量权威迁移（2026-08-19）

## v2 后续修复

本地 `jh_online_release` 的后续取证发现，历史 `role-count-authority-v1`
标记已经存在时，仍有 487 个账号的缓存数量与 `account_roles` 实际行数不一致。
其中 `21642502` 的 `format_version=8`，`role_count=2`，但连续的
`role_index=0,1,2` 主行有 3 条。实际标题链路
`1/1/12 -> 1/1/16 -> 1/1/4` 记录：关系 loader 失败后
`actorinfo.count=0`，客户端正常进入空角色页。

这证明 v1 的一次性标记无法覆盖标记提交之后导入或遗留的陈旧缓存。当前启动迁移改用
`role-count-authority-v2`：保留原有 InnoDB 事务、角色上限、连续索引和活动角色预检，
以 `COUNT(account_roles)` 更新 `account_role_state.role_count`，随后复核并写入 v2 标记。
它不创建、删除或移动角色，也不变更 `format_version`、角色属性、装备或背包。

隔离回归显式预先写入 v1 标记，再验证 v2 仍会修复 `3 -> 2` 和 `1 -> 0` 的缓存偏差；
因此服务器升级时可修复已有 v1 标记的数据库。实际客户端线包回归以
`guest00723` 和 `21642502` 运行，验证 `1/1/4.actorinfo` 的计数和角色 ID 与
`account_roles` 一致。

## 触发与业务链路

本地 `jh_online_release` 中账号 `202804723` 登录后，后台角色数据库打开路径返回
`role db unavailable`。该文本不是 MySQL 连接错误，而是以下关系加载链路失败后的
统一结果：

1. `account_role_state` 读取账号级 `active_role_id` 和 `role_count`；
2. `account_roles` 按 `role_index` 读取角色主行；
3. `vm_net_mock_role_db_load_mysql_relational()` 要求实际主行数严格等于元数据数量；
4. 数量不一致时加载返回失败，`g_vm_net_mock_role_db_valid` 被置为 `false`；
5. 账号管理打开路径返回 `role db unavailable`。

客户端角色列表和后续选角包必须来自完整的关系角色状态。服务端不能通过忽略校验、
伪造第五个角色或回放旧 payload 来绕过该失败。

## 原始证据

账号 `202804723`：

- `account_role_state.role_count = 5`；
- `account_roles` 只有 4 行，`role_index = 0,1,2,3`；
- `active_role_id = 10102`，且该角色主行存在；
- `role_id_sequence` 只有现存的 4 个角色；
- `account_role_state_payload_backup` 没有该账号；
- 背包、装备、技能、任务等角色子表没有第五个孤立 `role_id`。

全库一致性查询进一步显示：

- `account_role_state` 共 960 行；
- 其中 948 行满足 `role_count = COUNT(account_roles) + 1`；
- 异常行的 `updated_at` 均为 `2026-08-19 09:00:12`；
- `account_roles` 的实际最大账号角色数为 5，没有超过客户端上限；
- 所有现有账号的角色索引连续，活动角色引用也有效。

MySQL 的 `general_log` 与 `log_bin` 均关闭，数据库中没有相关 trigger 或 event。因此
无法从现存审计信息还原当时执行批量写入的具体进程或 SQL；第一处可证明的偏离是
`jh_online_release` 在上述同一时刻批量写入了错误角色数量。

## 已排除假设

- 不是账号本次登录造成：错误在角色 parser/响应构造前已经存在于关系表。
- 不是正常角色创建或删除的单账号事务残留：948 个账号同一时刻出现相同的 `+1`。
- 不是可恢复的第五个角色：角色序列、备份和子表均无对应身份。
- 不能用 `role_count = role_count - 1` 修复：该规则依赖这一次污染的外观，无法处理
  空账号、已一致账号或其他偏差；角色主表的实际聚合数才是权威值。

## 根因陈述

触发条件是服务加载一个 `account_role_state.role_count` 与该账号 `account_roles` 主行数
不一致的账号。被违反的契约是“角色数量元数据等于角色主表实际行数，且索引为
`0..count-1`”。第一处错误状态位于数据库元数据，加载器的失败只是对该损坏的正确拒绝。

## 迁移契约

服务器更新后，在监听客户端端口之前执行一次事务迁移：

1. 使用 `server_data_migrations` 的独立标记串行化并保证幂等；
2. 预检每个账号实际角色数不超过 5、角色索引连续、活动角色引用有效；
3. 以 `COUNT(account_roles)` 为唯一权威值更新 `account_role_state.role_count`；
4. 再次查询确认不存在数量差异；
5. 将迁移标记和数据修复在同一 InnoDB 事务中提交。

任一预检、更新或复核失败都回滚，并在服务接收客户端之前终止启动。迁移不创建、删除
或移动角色，不读取旧 payload，也不改变角色属性、背包、装备和活动角色。

这次迁移不提升 `account_role_state.format_version`：该字段表示角色 payload/关系字段的
结构版本，而本次没有改变角色结构。数据库数据语义版本由 `server_data_migrations`
独立记录。

## 验证计划

- 在隔离的 `cbe_auto_*` 数据库中构造 `3 -> 2`、`1 -> 0` 和已一致三种账号；
- 执行真实启动迁移，断言只把前两种数量修正为实际角色行数；
- 断言迁移标记只存在一行，重复执行不重复修改；
- 用真实关系加载器读取原 `3 -> 2` 账号，证明角色行数校验和角色数据库有效状态恢复；
- 执行 `make -j2`。

## 验证结果

- `make -j2` 成功生成 `bin/jh-online-server.exe`；
- `scripts/run-role-count-authority-migration-regression.ps1` 在独立
  `cbe_auto_role_count_*` 数据库中通过；
- 迁移日志记录 `corrected_accounts=2`，错误的 `3 -> 2` 和 `1 -> 0` 均按角色主表
  修正，已一致账号不变，角色主表总行数不变；
- 真实 `vm_net_mock_role_db_load_mysql_relational()` 随后成功读取两个角色及活动角色；
- 第二次执行返回 `action=already-applied`，迁移标记仍只有一行；
- 删除测试标记并制造非连续索引后，迁移以 `invalid_index=1` 拒绝，错误计数与标记均
  未被部分提交；
- 测试脚本完成后删除了隔离数据库，没有写入 `jh_online` 或 `jh_online_release`。

### v2 实测结果

- 修改前，`jh_online_release` 有 487 个 `role_count <> COUNT(account_roles)`；
  所有角色索引连续、活动角色引用有效、单账号角色数不超过 5。`21642502` 是其中
  唯一的 `2 -> 3` 账号，另有 484 个 `1 -> 0` 和 2 个 `1 -> 2` 缓存偏差；
- 新二进制启动时记录
  `role_count_authority_migration marker=role-count-authority-v2 corrected_accounts=487 max_roles=5 action=committed`；
  事务后复核不一致数为 0，`server_data_migrations` 同时保留 v1 与 v2 标记；
- `scripts/role-count-authority-migration-regression.c` 在临时 `cbe_auto_*` 数据库中通过，
  其中预先写入 v1 标记，证明 v2 的升级路径不会被旧标记跳过；
- `scripts/run-title-role-list-account-regression.ps1` 在自启的 `127.0.0.1:19342`
  服务上完成两个账号的真实 CBMS 序列。服务日志记录 `guest00723` 关系加载 2 角色、
  `21642502` 关系加载 3 角色；对应的 `1/1/4.actorinfo` 角色 ID 分别为
  `10550,10650` 和 `10036,10311,10384`，无 `mock_role_db_mysql_load_failed`。
