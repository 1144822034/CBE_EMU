# 江湖 OL mock-service MySQL 存储

服务端持久化使用本机 MySQL，默认连接参数如下：

- 主机：`127.0.0.1`
- 端口：`3306`
- 用户：`root`
- 数据库：`jh_online`

首次使用时需要手动执行建表脚本：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p < server/mysql/schema.sql
```

如果数据库仍使用旧版 `account_role_state.payload`，停止 mock-service 后执行一次：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p < server/mysql/migrate_payload_to_relational.sql
```

该脚本不会删除旧数据，而是把旧表重命名为 `account_role_state_payload_backup`。服务下次启动时会完成字段拆分和全服唯一角色 ID 的分配。

密码由命令行交互输入。服务运行时的默认密码与本机开发环境一致，也可以通过以下环境变量覆盖，避免修改源代码：

- `CBE_MYSQL_HOST`
- `CBE_MYSQL_PORT`
- `CBE_MYSQL_USER`
- `CBE_MYSQL_PASSWORD`
- `CBE_MYSQL_DATABASE`

## 表说明

- `accounts`：账号与登录密码。
- `friendships`：双向好友记录和好友列表显示属性。
- `account_role_state`：每个账号的活动角色和角色数量元数据。
- `account_roles`：角色基础属性、职业性别、等级、HP/MP、货币和场景坐标。
- `account_role_equipment`：按角色和装备槽保存的装备。
- `account_role_backpack`：按角色和背包槽保存的物品。
- `role_id_sequence`：分配全服唯一且不复用的角色 ID。
- `account_role_state_payload_backup`：旧二进制快照的只读迁移备份，不参与正常保存。

服务启动时会连接 MySQL 并验证这些表。关系表为空时，旧 payload 备份或 `bin/nvram` 服务端二进制文件仅作为一次性迁移来源读取；迁移完成后的正常保存只写关系表，不再写回 payload 或旧文件。

模拟器自身的 NVRAM、资源文件和更新缓存不属于服务端玩家数据，仍保持原有文件机制。
