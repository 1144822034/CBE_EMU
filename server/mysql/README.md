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

已有数据库升级到“任务可奖励多个物品”时执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_task_multi_rewards.sql
```

脚本新增 `server_task_reward_items`，不会修改已有任务或角色数据。没有该表行的
任务仍使用 `server_tasks.reward_item_*` 或原版 `task.dsh` 的单项奖励；后台保存过
的任务会把全部奖励按顺序写入新表。服务启动也会自动创建该表。

已有动态 NPC 任务绑定增加“完成后可重复接取”开关时，先停止 mock-service，且仅在
`server_dynamic_npc_tasks` 尚无 `repeatable` 列时执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_dynamic_npc_task_repeatable.sql
```

服务启动也会检查并补充该列；开关默认关闭，因此已有任务的完成后不可再次接取行为不会改变。

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

已有数据库增加标题服务器列表管理时，停止 mock-service 后执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_login_servers.sql
```

脚本新增 `server_login_servers`，只写入空库所需的“江湖一区 / 推荐”初始行；
已有行绝不会被覆盖。后台入口 `/admin-418yz6/?tab=servers` 可编辑服务器 ID、
显示名称、状态标签、24 位颜色、排序和启用状态。该配置驱动标题登录响应中的
服务器列表，不配置 CBMS 主机或端口，也不会让已建立的游戏连接切换到其他地址。

已有数据库升级到商品管理功能时执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_shop_items.sql
```

脚本只新增商品价格和上下架覆盖表，不会修改 `item.dsh`、`equip.dsh`
或角色背包数据。服务启动时也会自动执行同等的 `CREATE TABLE IF NOT EXISTS`。

已有数据库增加宝箱奖池管理时执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_chest_rewards.sql
```

脚本只新增 `server_chest_rewards`。青铜、白银、黄金宝箱及其钥匙的对应关系来自
`item.dsh`，但客户端资源不含官方奖池或概率，故不会写入猜测的默认掉落。请在后台
`/admin-418yz6/?tab=chests` 配置每个宝箱的物品、数量与相对权重；未配置时开箱不会
消耗宝箱或钥匙。服务启动也会自动创建该表。

已有数据库升级到怪物管理功能时执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_monster_management.sql
```

脚本只新增怪物属性覆盖表。没有覆盖记录的怪物继续使用服务端目录中的
等级、类型和统一属性公式；服务启动时也会自动创建该表。

已有怪物管理升级为多物品掉落时，停止 mock-service 后执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_monster_multi_drops.sql
```

脚本把旧 `server_monsters.drop_item_id/drop_rate_percent` 的单条配置复制到
`server_monster_drops` 的第 1 槽，不会覆盖已经存在的多掉落配置。之后应在
后台“怪物管理”中保存一次该怪物；新保存会以多掉落表为唯一来源，并清空旧列，
避免已删除的旧掉落在重启后被重新导入。

已有数据库升级到装备强化功能时，停止 mock-service 后执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_equipment_enhancement.sql
```

脚本只为 `account_role_backpack` 增加 `enhance_level` 字段，已有装备
默认强化等级为 0。

已有数据库升级到装备实例状态（强化和耐久随穿戴、换装、交易及重登保持一致）时，
先停止 mock-service，并在尚未添加这四列时执行一次：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_equipment_instance_state.sql
```

脚本把 `enhance_level`、`durability`、`durability_max` 纳入
`account_role_equipment`，并把背包行的当前/最大耐久补入
`account_role_backpack`。历史 `account_role_equipment_durability` 只会在首次
登录时作为同一 `item_id` 的迁移来源；迁移成功后不再是运行时数据源。服务启动也会
逐列检查并补齐表结构，但生产升级建议在停服窗口手动执行以上脚本。

已有数据库升级到动态 NPC 商店、修理和技能导师功能时执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_npc_services.sql
```

脚本新增角色装备耐久和已学技能关系表，不修改现有角色 payload、背包或
装备槽。服务启动时也会自动执行同等的 `CREATE TABLE IF NOT EXISTS`。

已有动态 NPC 配置使用过旧 `n_girl.actor` 时，停止 mock-service 后执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_normalize_dynamic_npc_actors.sql
```

该迁移仅把动态 NPC 的 Actor 设置更新为兼容的 `n_woman1.actor`，不会删除 NPC、
坐标、XSE、任务或副本绑定。后台此后不会再提供 `n_girl.actor` 作为动态 NPC 的
可选模型；未迁移的旧行会被运行时停用并提示管理员修正，而不会下发给客户端。

已有动态 NPC 需要自定义客户端对话中“服务入口”的名称和说明时，停止 mock-service
后执行一次：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_dynamic_npc_dialog_options.sql
```

脚本只给 `server_dynamic_npcs` 增加 `service_option_name` 和
`service_option_description` 两列；它们只影响已选“对话服务功能”在客户端 NPC 对话
中的显示文字，不会改变服务类型、商品、修理、技能或任务流程。服务启动也会检查并补齐
这两列；已由服务自动补齐时不要重复执行该一次性脚本。

动态 NPC 或原生 NPC 覆盖需要同时提供多个服务（例如接任务同时副本传送、同时经营
武器和药品）时，停服执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_npc_multiple_services.sql
```

该脚本建立 `server_npc_services`。旧的单一 `npc_kind` / `service_kind` 不会被批量改写：
没有对应关系行时，服务仍按旧单服务兼容；通过新后台保存某个 NPC 后，才为该 NPC 写入
完整服务集合。每个服务可单独填写对话选项名称和说明，留空即使用服务默认文案。`service_kind=0`
是“已显式配置但没有直连服务”的内部标记，不能手工作为业务服务使用。

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

已有数据库升级修炼天书实例说明时执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_training_books.sql
```

脚本只新增 `account_role_training_books`。服务会在角色首次读取 921
“修炼天书”时，按背包的 `item_seq` 自动补齐已有实例；不会重置已有角色或背包。

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
- `server_login_servers`：标题登录页显示的服务器 ID、名称、状态、颜色、排序和启用状态。
- `server_monsters`：怪物属性覆盖；旧版单掉落列仅保留用于升级导入。
- `server_monster_drops`：怪物的有序多掉落配置；每一行独立按配置概率投掷。
- `friendships`：双向好友记录和好友列表显示属性。
- `account_role_state`：每个账号的活动角色和角色数量元数据。
- `account_roles`：角色基础属性、职业性别、等级、HP/MP、货币和场景坐标。
- `account_role_equipment`：按角色和装备槽保存装备实例的物品 ID、强化等级、当前/最大耐久。
- `account_role_equipment_durability`：旧版耐久表，只在首次实例迁移时按同一物品 ID 读取，不参与运行时保存。
- `account_role_skills`：按角色保存已学习技能和技能等级。
- `account_role_backpack`：按角色和背包槽保存物品实例、数量、强化等级和当前/最大耐久；802/803 的 `item_count` 分别表示剩余 HP/MP 储量。
- `account_role_training_books`：921 修炼天书的按账号、角色、背包序号持久化的标题、说明、等级与经验实例数据。
- `account_role_tasks`：按角色保存任务状态和两组任务进度。
- `server_tasks`：后台编辑过的 `task.dsh` 覆盖项及新增任务定义、首项奖励和三阶段 NPC 对话。
- `server_task_reward_items`：任务的有序多项物品奖励；存在记录时覆盖 `server_tasks` 的首项奖励兼容字段。
- `server_dynamic_npc_tasks`：动态 NPC 到一个可接取任务的绑定关系，以及该 NPC 是否允许角色在完成后重复接取。
- `server_dynamic_npcs`：服务端动态 NPC 的场景位置、Actor、任务/XSE 与旧单服务兼容字段；新服务集合优先保存于 `server_npc_services`。
- `server_npc_services`：按场景和 Actor 保存动态 NPC 或原生 NPC 覆盖的有序多服务集合，以及每项可选的对话名称/说明；只允许现有 parser-backed `action=1` 服务种类，任务仍由动态 NPC 任务绑定独立生成 `action=4`。
- `role_id_sequence`：分配全服唯一且不复用的角色 ID。
- `guilds`：帮派名称、帮主、等级、人数上限、资源、建设和公告。
- `guild_members`：角色与帮派的一对一成员关系及职位。
- `guild_applications`：待处理、已同意或已拒绝的入帮申请。
- `server_shop_items`：后台覆盖的商品价格、上下架状态和商城分区。`shop_section=0` 使用 DSH 默认分区，`1` 放入秘宝道具，`2` 强制作为普通商品；没有记录的物品继续使用 DSH 默认价格并默认上架。
- `server_chest_rewards`：青铜、白银、黄金宝箱的有序奖池。一次开箱按同一宝箱全部行的相对 `weight` 抽取恰好一项，`item_count` 为该项数量；没有行时不开箱、不消耗。
- `server_monsters`：后台保存的怪物等级、类型、战斗属性、奖励和掉落覆盖；没有记录的怪物继续使用服务端目录默认公式。
- `account_role_state_payload_backup`：旧二进制快照的只读迁移备份，不参与正常保存。

服务启动时会连接 MySQL 并验证这些表。首次完成关系表迁移后，服务会在
`server_data_migrations` 写入 `mysql-authoritative-v1` 标记。标记写入前，旧 payload
备份或 `bin/nvram` 服务端二进制文件仅可作为一次性迁移来源读取；标记写入后 MySQL
关系表是唯一权威来源，服务不会在重启时再次回放旧快照。若已封印的数据库缺少关系行，
请从备份恢复或显式执行迁移，不要依赖旧快照自动覆盖当前数据。

模拟器自身的 NVRAM、资源文件和更新缓存不属于服务端玩家数据，仍保持原有文件机制。
