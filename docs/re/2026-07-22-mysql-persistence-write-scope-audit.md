# MySQL 持久化写入范围审计

## 范围与方法

审计 src/server 的 DELETE、事务、全量 upsert 及其调用方，区分一次性迁移、
需要原子替换的业务事务，以及日常单条状态变更。

## 已确认的不合理路径

vm_mock_service_friend_db_save 在好友邀请同意后执行 DELETE FROM friendships，
再将整个内存好友数组重新插入。好友确认只会创建或刷新一对双向关系；删除全表会
使写入量随所有账户的好友总数增长，并会在并发请求下覆盖其他请求刚写入的关系。
正常路径应只在一个事务内 upsert 这两条方向记录，数据库成功后才保留内存修改。

vm_net_mock_role_db_save 由移动、战斗、道具、背包和装备等高频操作调用，却会
通过全量关系保存器重写当前账号的所有角色、装备和背包。它不跨账号，但普通操作
仍不应触碰未变更角色。正常调用者都只变更当前活动角色；角色创建、删除、加载迁移
和规范化保留全量保存器。普通保存应改为仅同步活动角色及账户活动角色状态。

## 已排除的删除语句

交易流程删除两个参与角色的背包后在同一事务内写回，目的是保证物品和钱币交换的
原子性；帮派、任务、动态 NPC、怪物删除均带有具体主键或业务条件。它们不是全局
快照重写，不能仅因存在 DELETE 而改成无事务单行操作。

## 验证标准

好友确认仅写两条定向关系；角色移动或改背包不再删除同账号其他角色的装备或背包。
注册、好友、角色、背包和装备的现有响应契约保持不变；所有修改后编译并进行本地
HTTP/数据库回归。

## 角色保存的实施边界

角色数据最多 5 个角色、每个背包最多 200 格；普通保存仍可能高频触发，因而
不能继续按账号删除所有角色的装备和背包。保存器将显式区分 full_snapshot：
创建、删除、旧数据迁移和启动规范化使用完整调和；普通 vm_net_mock_role_db_save
使用 active scope，只 upsert 当前活动角色，并只删除和重建该角色的装备、背包行。
账号级 active_role_id 与 role_count 仍在同一事务内保存。这样不会误删其他角色
的关联数据，也不会将角色创建或删除误当作局部更新。

## 验证结果

静态调用检查确认：vm_mock_service_friend_db_save_all 只保留给 legacy-migrate 或
启动 normalize；好友同意改走 mock_friend_db_mysql_pair，事务内只有两条 upsert。
本地 MySQL 事务验证双向 upsert 时 friendships 行数从 2 临时变为 4，回滚后恢复
为 2。

普通 vm_net_mock_role_db_save 传入 active scope；role-create、role-delete、
legacy-relational-migrate、payload-relational-migrate 和 normalize 显式传入
full snapshot。活动范围的 DELETE 均附带 account_id 与 role_id，不会命中同账号
其他角色。
