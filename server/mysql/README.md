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

服务升级到角色数量权威迁移后无需手工执行 SQL。新服务会在监听客户端端口前，按
`account_roles` 的实际行数事务化修正 `account_role_state.role_count`，并写入
`server_data_migrations.role-count-authority-v1`。迁移前会校验每个账号最多 5 个角色、
角色索引连续且活动角色仍然存在；发现真实角色结构损坏时服务拒绝启动，不会伪造角色
或回放旧 payload。迁移提交后重复启动只检查标记，不会重复修改。

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

已有数据库升级到“场景战斗怪”配置层时执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_scene_battle_monsters.sql
```

脚本仅增加战斗怪草稿、场景基础资源快照和部署状态表。保存草稿不会修改任何
`.sce`；只有后台显式部署时，服务端才会从快照重建相应的 SCE2 战斗记录。服务启动
时也会自动创建同名表。

已有数据库升级到启动期“游戏数据内容更新”时执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_content_updates.sql
```

脚本新增 `server_content_update_releases` 和
`server_content_update_files`，用来保存 WT 18/9 → 18/8 的版本清单。清单可以包含
任意非 CBM 游戏数据资源。每个文件还保存服务端的发布时字节校验，用于避免相同文件的
重复发布。服务不会在每次启动时递增版本，只有后台新增/移除资源或检测到已发布资源的
字节变化时，客户端才会因 `id/code` 不同而删除并重取其中资源。服务启动也会自动补齐
旧表的 `resource_checksum` 列，并一次性迁移旧 `server_content_update.tsv`（若存在）。

已有数据库升级到用户账号中心和旧版数据库后台密码时执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_web_accounts.sql
```

脚本新增 `server_admin_config`，这是旧版单一后台密码的兼容来源；不会修改游戏账号
或角色数据。完成下面的多账号迁移后，后台入口 `/admin-418yz6/` 改为使用独立的
后台账号登录，不再读取玩家 `accounts` 表。

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_admin_users.sql
```

迁移会将原有共享密码平滑复制为默认后台账号 `admin`，不会覆盖已有 `admin` 记录；
请立即为每位操作者创建独立账号并修改默认密码。账号名仅允许字母、数字、`. _ - @`：

```sql
INSERT INTO server_admin_users
  (account_id, password_value, failed_attempts, locked)
VALUES
  ('operator.alice', '请替换为强密码', 0, 0);
```

每个后台账号独立计算连续失败次数；错误 5 次只会锁定该账号。解锁或改密时同时清除
该账号的失败状态：

```sql
UPDATE server_admin_users
SET password_value = '新密码', failed_attempts = 0, locked = 0
WHERE account_id = 'operator.alice';
```

已有数据库启用游戏与网页共用的来源 IP 登录封锁时执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_login_ip_blocks.sql
```

游戏客户端、玩家账号中心和后台管理登录共用 `server_login_ip_blocks`：同一来源 IPv4
连续凭据错误 15 次会被永久封锁；第 15 次错误仍会按原登录入口返回失败结果，之后游戏服务会在
读取协议帧前关闭连接；网页服务会先读取受限长度的请求头以识别受信任代理传递的真实 IP，随后
不读取正文、不处理路由且不发送 HTTP 响应。一次成功的凭据登录会清除
未封锁 IP 的连续失败计数；已经封锁的 IP 不会被登录自动解锁。需要人工恢复时，在可信的
管理终端执行：

```sql
DELETE FROM server_login_ip_blocks WHERE ip_address = '203.0.113.7';
```

服务启动时会把已封锁 IP 载入内存，因此手工删除后需重启服务使该 IP 立即恢复访问。

网页入口经 nginx 反向代理时，nginx 必须覆盖客户端可控的转发头：

```nginx
proxy_set_header X-Real-IP $remote_addr;
proxy_set_header X-Forwarded-For $remote_addr;
```

执行以下迁移后，可在后台“安全设置”新增 nginx、负载均衡器或 CDN 的实际 TCP 来源 IPv4，
分别勾选可采信的 `X-Real-IP` 和 `X-Forwarded-For`，保存立即生效：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_trusted_proxy_sources.sql
```

初始仅包含同机 nginx 的 `127.0.0.1`。服务端优先使用 `X-Real-IP`；`X-Forwarded-For` 仅在
已勾选且为单个 IPv4 时使用，带逗号的转发链会被拒绝。未启用或未列入后台白名单的连接提供
的转发头都会被忽略。`CBE_MOCK_TRUSTED_PROXY_IPV4` 仅保留为旧部署在无数据库条目前的
兼容兜底，新的部署应使用后台配置。游戏 TCP 不使用 HTTP 请求头，仍按 TCP 对端地址封锁。

已有数据库启用后台“称号管理”时执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_role_designations.sql
```

脚本只新增 `server_role_designations`。服务首次打开称号页或处理称号请求时，会以
`INSERT IGNORE` 写入现有金钱/等级称号及四个特殊称号（资深老友、圣诞骑士、武林传奇、
勇者王）的默认条件；不会改变角色已装备的称号。特殊称号默认停用。圣诞骑士固定要求
全套圣诞装、武林传奇固定要求全套武林装；二者只可启用或停用，不能改成金钱或等级门槛。
另两个特殊称号仍使用后台配置的金钱/等级条件，直到补充其独立的游戏规则。

升级已有称号配置后还应执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_role_designation_equipment_sets.sql
```

该脚本只将圣诞骑士（ID 33）和武林传奇（ID 34）过去临时使用的等级条件改为固定套装条件；
不会改动角色当前装备或其他称号配置。服务端在读取称号配置时也会重复校正这两条规则，避免
遗漏迁移的旧部署继续按等级错误解锁。

已有数据库增加 W 币充值功能时执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_wcoin_recharge.sql
```

脚本新增支付配置和充值订单表，不会改动已有账号、角色或 W 币余额。通讯密钥
只保存在 `server_payment_config.secret_key`，不要写入网页、日志或提交到源码。
`callback_base_url` 应填写外网能够访问账号中心的地址；留空时会使用支付后台配置
的回调地址，并由订单状态查询返回的签名数据提供兜底确认。

异步通知地址为 `/payment/cbhub/notify`。它兼容 `GET` 查询参数和监控 App 常用的
`POST application/x-www-form-urlencoded` 请求体；同步返回地址 `/payment/cbhub/return`
仍只接受 `GET`。异步通知必须带齐 `payId`、`param`、`type`、`price`、`reallyPrice`、`sign`，
其中签名为 `MD5(payId + param + type + price + reallyPrice + 通讯密钥)`，没有分隔符且金额文本
必须保持参与签名时的原样（例如 `1.00` 不能改写为 `1`）。回调只能确认本服务先前创建的订单；
成功响应正文固定为 `success`，其他校验失败返回 `error_sign`。服务日志会记录不含密钥的
失败原因，如 `callback-fields-invalid`、`signature-rejected` 或 `order-rejected-or-credit-failed`。

已有数据库增加用户中心“角色迁移”功能时执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_role_transfers.sql
```

迁出会生成一个仅可使用一次、有效期 15 分钟的 8 位验证码。迁入在单个事务内完成，
会保留角色属性、背包、装备、技能、任务、好友、帮派与聊天历史；账号级 W 币、充值
订单和登录资料不会迁移。迁入成功后服务会断开两边该账号的游戏连接，避免旧会话写回
已迁出的角色数据。服务也会在首次使用迁移功能时自动创建同名表。

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

已有数据库启用后台账号操作日志时执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_admin_operation_logs.sql
```

脚本只新增 `server_admin_operation_logs`。后台设置角色等级、增加普通钱币或 W 币、
发放物品/装备、改名、改密和重置位置等成功操作，以及游戏内商城购买和付费副本门票
的 W 币成功扣款，都会追加记录；角色后续迁移或删除不会改写既有审计记录。服务首次
写入或查看该页面时也会自动创建同一张表。

已有怪物管理升级为多物品掉落时，停止 mock-service 后执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_add_monster_multi_drops.sql
```

脚本把旧 `server_monsters.drop_item_id/drop_rate_percent` 的单条配置复制到
`server_monster_drops` 的第 1 槽，不会覆盖已经存在的多掉落配置。之后应在
后台“怪物管理”中保存一次该怪物；新保存会以多掉落表为唯一来源，并清空旧列，
避免已删除的旧掉落在重启后被重新导入。

已有数据库需要在怪物掉落概率中使用小数（例如 `0.25%`）时，建议在停服窗口执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_monster_drop_rate_decimal.sql
```

脚本把掉落概率列升级为两位小数，原有整数概率保持数值不变（例如 `5` 变为
`5.00`）。服务启动时也会检测并完成同一列类型升级。

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

强化词条改为装备实例在 `+4/+8/+12/+16` 到达时随机生成、数值小范围波动并持久化后，
已有数据库可在停服窗口执行：

```powershell
mysql -h 127.0.0.1 -P 3306 -u root -p jh_online < server/mysql/migrate_random_equipment_affixes.sql
```

服务启动也会自动补齐 `enhance_affix_types` 与 `enhance_affix_values` 两列。旧的已强化
装备以零值标记；首次加载时服务端会为其已解锁的阶段各抽取一次词条，并在同一角色
事务中写回。之后穿戴、卸下、交易和重新登录都保留同一组结果。

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
- `server_admin_config`：旧版单一后台密码的兼容迁移来源；升级后不再用于登录验证。
- `server_admin_users`：独立后台操作员账号、密码、连续失败次数和锁定状态；与玩家账号表隔离。
- `server_login_ip_blocks`：游戏、账号中心和后台管理共用的来源 IP 连续登录失败计数与封锁状态。
- `server_trusted_proxy_sources`：网页反向代理的可信 TCP 来源、可采信的真实 IP 请求头及启用状态。
- `server_admin_operation_logs`：后台对账号和角色执行成功操作、以及游戏内成功 W 币消费的追加式审计记录，包含操作人（后台账号或游戏内）、时间、目标账号/角色、金额或物品信息及说明。
- `server_role_designations`：称号启用状态与达成门槛；名称、稳定 ID 和客户端徽章资源仍由服务端已验证目录固定。
- `server_payment_config`：支付接口地址、通讯密钥、公开回调地址和 W 币兑换比例。
- `wcoin_recharge_orders`：充值订单、支付确认及幂等入账状态。
- `server_data_migrations`：记录一次性数据语义迁移，防止重复换算。
- `server_login_servers`：标题登录页显示的服务器 ID、名称、状态、颜色、排序和启用状态。
- `server_monsters`：怪物属性覆盖；旧版单掉落列仅保留用于升级导入。
- `server_monster_drops`：怪物的有序多掉落配置；每一行独立按配置概率投掷。
- `friendships`：双向好友记录和好友列表显示属性。
- `account_role_state`：每个账号的活动角色和角色数量元数据。
- `account_roles`：角色基础属性、职业性别、等级、HP/MP、货币和场景坐标。
- `account_role_transfer_codes`：用户中心角色迁入/迁出的单次、限时验证码；验证码仅关联迁出角色，不保存目标账号。
- `account_role_equipment`：按角色和装备槽保存装备实例的物品 ID、强化等级、四阶段随机词条和当前/最大耐久。
- `account_role_equipment_durability`：旧版耐久表，只在首次实例迁移时按同一物品 ID 读取，不参与运行时保存。
- `account_role_skills`：按角色保存已学习技能和技能等级。
- `account_role_backpack`：按角色和背包槽保存物品实例、数量、强化等级、四阶段随机词条和当前/最大耐久；802/803 的 `item_count` 分别表示剩余 HP/MP 储量。
- `account_role_vitality`：按账号、角色保存独立于 HP/MP 的活力当前值与上限；聚元丹 833 仅在未满时恢复 100 点，任务/战斗/复活状态包均从此表读取。
- `account_role_training_books`：921 修炼天书的按账号、角色、背包序号持久化的标题、说明、等级与经验实例数据。
- `account_role_tasks`：按角色保存任务状态和两组任务进度。
- `server_tasks`：后台编辑过的 `task.dsh` 覆盖项及新增任务定义、首项奖励和三阶段 NPC 对话。
- `server_task_reward_items`：任务的有序多项物品奖励；存在记录时覆盖 `server_tasks` 的首项奖励兼容字段。
- `server_dynamic_npc_tasks`：动态 NPC 到一个可接取任务的绑定关系，以及该 NPC 是否允许角色在完成后重复接取。
- `server_dynamic_npcs`：服务端动态 NPC 的场景位置、Actor、任务/XSE 与旧单服务兼容字段；新服务集合优先保存于 `server_npc_services`。
- `server_npc_services`：按场景和 Actor 保存动态 NPC 或原生 NPC 覆盖的有序多服务集合，以及每项可选的对话名称/说明；只允许现有 parser-backed 对话服务种类，任务仍由动态 NPC 任务绑定独立生成 `action=4`，守关怪挑战使用客户端原生 `action=13`。
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
