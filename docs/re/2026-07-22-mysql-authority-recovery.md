# MySQL 权威存储与异常退出恢复（2026-07-22）

## 触发与观察

线上曾观察到 mock-service 闪退并重新启动后，账号/角色列表回到了很早以前的状态。
这不是客户端协议或角色选择包的问题；重新启动时服务端的持久化加载路径已经先于
`WT 1/1` 角色列表响应执行。

本机 `jh_online` 取证结果：

- `accounts`、`friendships`、`account_role_state`、`account_roles`、装备和背包表均为
  `InnoDB`；
- `@@autocommit=1`、`@@innodb_flush_log_at_trx_commit=1`、`@@sync_binlog=1`；
- `account_role_state_payload_backup` 仍保留初始迁移时的旧快照，时间早于当前关系表。

因此，正常 InnoDB `COMMIT` 后仅 mock-service 异常退出不应回滚已提交数据。

## 根因

关系存储已经上线后，两个迁移兜底仍是无限期可用的：

1. `vm_net_mock_role_db_load()` 在找不到 `account_role_state` 行时仍会读取
   `account_role_state_payload_backup`，然后把其旧二进制内容重新拆分写入关系表；
2. `accounts` 或 `friendships` 为空时仍会无条件读取旧 `nvram` 快照。

这违反了“旧数据仅用于一次性迁移”的约束。任何关系数据缺失都会被误判为首次迁移，
从而出现看似被恢复到旧版本的现象。另有同类可靠性问题：账号/好友整表事务保存的
失败结果此前未返回给创建、改密和加好友调用方，后台可能显示成功但内存状态并未落库。

## 修复

- 启动时校验核心持久化表均为 `InnoDB`，并建立/读取
  `server_data_migrations.mysql-authoritative-v1`。
- 首次启动仍允许从旧文件或 payload 迁移全部账号；全部角色关系数据成功检查后写入
  权威标记。以后重启只读取关系表，绝不再次回放旧文件或 payload 备份。
- 空 `accounts` 表若同时存在角色关系数据或 payload 备份，服务会作为完整性错误拒绝
  启动，而非覆盖为旧账号快照。
- 账号创建、改密、好友双向写入、后台加 W 币/货币、后台物品给予、角色选择/删除和
  场景位置写入都检查关系保存结果；失败时恢复调用方内存副本，不再报告成功。
- 原有角色、交易、帮派和充值的多表提交路径继续使用单个 MySQL 事务；任务进度、世界
  消息等单行写入依赖已验证的 MySQL autocommit。

## 验证

1. `make -j2` 成功。
2. 服务首次按新逻辑启动后写入 `mysql-authoritative-v1`；强制结束进程并重启，账号数、
   角色状态数、角色数和好友数保持不变，启动日志为 `mysql_authority_prepare sealed=1`。
3. 隔离测试账号只插入伪造且无效的旧 payload、不给关系状态。在已封印服务重启后，
   服务正常创建空的关系状态而没有读取该 payload；随后测试账号、关系行和 payload
   均已删除。

## 边界

如果已封印数据库真的丢失了某个关系行，服务不会从旧快照自动“修复”。应从备份恢复
或明确执行人工迁移；这避免把旧数据误当成最新玩家状态。
