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

已有数据库升级到后台任务管理与动态 NPC 任务绑定时执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_task_management.sql
```

脚本新增 `server_tasks` 和 `server_dynamic_npc_tasks`。原版 `task.dsh`
不会导入或改写；后台只保存编辑覆盖项和新增任务。服务启动时也会自动执行同等的
`CREATE TABLE IF NOT EXISTS`。

已有数据库升级到用户账号中心和数据库后台密码时执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_web_accounts.sql
```

脚本新增 `server_admin_config`，不会修改游戏账号或角色数据。后台入口为
`/admin-418yz6/`，管理密码、连续失败次数和锁定状态都从该表读取。默认密码只在
首次建表时写入为 `123456`，已有配置不会被覆盖。连续错误 5 次后，即使输入正确
密码也无法登录，执行下面的 SQL 可解锁：

```sql
UPDATE server_admin_config
SET failed_attempts = 0, locked = 0
WHERE config_id = 1;
```

修改密码时建议同时清除失败次数和锁定状态：

```sql
UPDATE server_admin_config
SET password_value = '新密码', failed_attempts = 0, locked = 0
WHERE config_id = 1;
```

已有数据库增加 W 币充值功能时执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_wcoin_recharge.sql
```

脚本新增支付配置和充值订单表，不会改动已有账号、角色或 W 币余额。通讯密钥
只保存在 `server_payment_config.secret_key`，不要写入网页、日志或提交到源码。
`callback_base_url` 应填写外网能够访问账号中心的地址；留空时会使用支付后台配置
的回调地址，并由订单状态查询返回的签名数据提供兜底确认。

已有数据库升级到商品管理功能时执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_shop_items.sql
```

脚本只新增商品价格和上下架覆盖表，不会修改 `item.dsh`、`equip.dsh`
或角色背包数据。服务启动时也会自动执行同等的 `CREATE TABLE IF NOT EXISTS`。

已有数据库升级到怪物管理功能时执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_monster_management.sql
```

脚本只新增怪物属性覆盖表。没有覆盖记录的怪物继续使用服务端目录中的
等级、类型和统一属性公式；服务启动时也会自动创建该表。

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

已有数据库升级逍遥壶/神仙壶剩余容量语义时，停止 mock-service 后执行一次：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_vitality_flask_reserve.sql
```

迁移会把旧服务端为 802/803 保存的普通物品数量换算成每壶 50000 点容量，
并通过 `server_data_migrations` 保证脚本重复执行时不会再次放大容量。新获得的
神仙壶保存剩余 HP，逍遥壶保存剩余 MP；只在剩余值归零时删除背包行。

密码由命令行交互输入。服务运行时的默认密码与本机开发环境一致，也可以通过以下环境变量覆盖，避免修改源代码：

- `CBE_MYSQL_HOST`
- `CBE_MYSQL_PORT`
- `CBE_MYSQL_USER`
- `CBE_MYSQL_PASSWORD`
- `CBE_MYSQL_DATABASE`

## 表说明

- `accounts`：账号与登录密码。
- `server_admin_config`：后台管理密码、连续失败次数和数据库锁定状态。
- `server_payment_config`：支付接口地址、通讯密钥、公开回调地址和 W 币兑换比例。
- `wcoin_recharge_orders`：充值订单、支付确认及幂等入账状态。
- `server_data_migrations`：记录一次性数据语义迁移，防止重复换算。
- `friendships`：双向好友记录和好友列表显示属性。
- `account_role_state`：每个账号的活动角色和角色数量元数据。
- `account_roles`：角色基础属性、职业性别、等级、HP/MP、货币和场景坐标。
- `account_role_equipment`：按角色和装备槽保存的装备。
- `account_role_equipment_durability`：按装备槽和当前物品保存耐久度。
- `account_role_skills`：按角色保存已学习技能和技能等级。
- `account_role_backpack`：按角色和背包槽保存物品、数量及装备强化等级；802/803 的 `item_count` 分别表示剩余 HP/MP 储量。
- `account_role_tasks`：按角色保存任务状态和两组任务进度。
- `server_tasks`：后台编辑过的 `task.dsh` 覆盖项及新增任务定义、奖励和三阶段 NPC 对话。
- `server_dynamic_npc_tasks`：动态 NPC 到一个可接取任务的绑定关系。
- `role_id_sequence`：分配全服唯一且不复用的角色 ID。
- `guilds`：帮派名称、帮主、等级、人数上限、资源、建设和公告。
- `guild_members`：角色与帮派的一对一成员关系及职位。
- `guild_applications`：待处理、已同意或已拒绝的入帮申请。
- `server_shop_items`：后台覆盖的商品价格和上下架状态；没有记录的物品继续使用 DSH 默认价格并默认上架。
- `server_monsters`：后台保存的怪物等级、类型、战斗属性、奖励和掉落覆盖；没有记录的怪物继续使用服务端目录默认公式。
- `account_role_state_payload_backup`：旧二进制快照的只读迁移备份，不参与正常保存。

服务启动时会连接 MySQL 并验证这些表。首次完成关系表迁移后，服务会在
`server_data_migrations` 写入 `mysql-authoritative-v1` 标记。标记写入前，旧 payload
备份或 `bin/nvram` 服务端二进制文件仅可作为一次性迁移来源读取；标记写入后 MySQL
关系表是唯一权威来源，服务不会在重启时再次回放旧快照。若已封印的数据库缺少关系行，
请从备份恢复或显式执行迁移，不要依赖旧快照自动覆盖当前数据。

模拟器自身的 NVRAM、资源文件和更新缓存不属于服务端玩家数据，仍保持原有文件机制。
