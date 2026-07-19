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

已有数据库升级到帮派功能时，停止 mock-service 后执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_guilds.sql
```

脚本只新增帮派、成员和申请表，不会修改已有账号、角色或好友数据。

已有数据库升级到任务功能时，停止 mock-service 后执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_tasks.sql
```

脚本只新增按账号、角色保存的任务状态表，不会修改已有角色数据。

已有数据库升级到商品管理功能时执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_shop_items.sql
```

脚本只新增商品价格和上下架覆盖表，不会修改 `item.dsh`、`equip.dsh`
或角色背包数据。服务启动时也会自动执行同等的 `CREATE TABLE IF NOT EXISTS`。

已有数据库升级到装备强化功能时，停止 mock-service 后执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_equipment_enhancement.sql
```

脚本只为 `account_role_backpack` 增加 `enhance_level` 字段，已有装备
默认强化等级为 0。

已有数据库升级到动态 NPC 商店、修理和技能导师功能时执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_npc_services.sql
```

脚本新增角色装备耐久和已学技能关系表，不修改现有角色 payload、背包或
装备槽。服务启动时也会自动执行同等的 `CREATE TABLE IF NOT EXISTS`。

将旧版蓬莱初始场景别名统一为 `c00蓬莱仙岛_01.sce` 时，停止
mock-service 后执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_initial_scene.sql
```

该迁移只修改 `00_蓬莱仙岛01[.sce]` 和缺少扩展名的
`c00蓬莱仙岛_01`，不会改变处于其他场景的角色。

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
- `account_role_equipment_durability`：按装备槽和当前物品保存耐久度。
- `account_role_skills`：按角色保存已学习技能和技能等级。
- `account_role_backpack`：按角色和背包槽保存物品、数量及装备强化等级。
- `account_role_tasks`：按角色保存任务状态和两组任务进度。
- `role_id_sequence`：分配全服唯一且不复用的角色 ID。
- `guilds`：帮派名称、帮主、等级、人数上限、资源、建设和公告。
- `guild_members`：角色与帮派的一对一成员关系及职位。
- `guild_applications`：待处理、已同意或已拒绝的入帮申请。
- `server_shop_items`：后台覆盖的商品价格和上下架状态；没有记录的物品继续使用 DSH 默认价格并默认上架。
- `account_role_state_payload_backup`：旧二进制快照的只读迁移备份，不参与正常保存。

服务启动时会连接 MySQL 并验证这些表。关系表为空时，旧 payload 备份或 `bin/nvram` 服务端二进制文件仅作为一次性迁移来源读取；迁移完成后的正常保存只写关系表，不再写回 payload 或旧文件。

模拟器自身的 NVRAM、资源文件和更新缓存不属于服务端玩家数据，仍保持原有文件机制。
