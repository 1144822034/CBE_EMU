# 宝箱奖励的世界频道播报

## 配置与持久化契约

`server_chest_rewards` 的每一条奖励记录新增
`world_broadcast TINYINT UNSIGNED NOT NULL DEFAULT 0`。它是**奖励行**属性，
而非宝箱整体属性：同一宝箱可只播报稀有奖品。

后台“宝箱管理”的每行新增“世界播报”复选框；未勾选或旧表迁移后的值均为 `0`。
已有部署会在服务端首次加载宝箱配置时通过 `information_schema` 检查并执行一次
MySQL 5.x 兼容的 `ALTER TABLE`。`server/mysql/migrate_add_chest_world_broadcast.sql`
仅供手工管理 schema 时运行一次。

## 发送时机与世界消息链路

原生开箱请求仍为 `WT 1/7/15`，其背包变更成功的既有响应仍为：

```text
1/7/1 acknowledgement
→ 1/7/7 type=2 + 1/7/11（宝箱）
→ 1/7/7 type=2 + 1/7/11（钥匙）
→ 1/7/7 type=1（奖励）
```

播报不添加到这个响应中，避免改变已经验证的物品 parser 对象顺序。仅当：

1. 配置的中奖行 `world_broadcast=1`；
2. 奖励已写入角色状态；且
3. `vm_net_mock_role_db_save("chest-open")` 成功；

服务端才调用现有世界消息的“先写 MySQL、再向在线会话队列投递”链路。公告以
“系统”身份显示，消息正文为 GBK：

```text
恭喜玩家【xx】开启黄金宝箱获得xxx
```

当配置数量大于 1 时，末尾追加 `×数量`；因此公告能够准确表示实际奖励。历史表保留
开箱角色的非零 role id（显示名仍为“系统”），满足既有最近 30 条世界消息的重放查询。

世界消息写入失败不会回滚已提交的角色开箱事务；会记录
`chest_world_broadcast_failed`，不会向在线玩家发送一条无法由历史重放的假公告。

## 回归范围

`scripts/chest-world-broadcast-regression.c` 是无副作用的静态回归：验证黄金宝箱的
GBK 名称、单件模板和多件数量后缀。它不启动监听器、不连接 MySQL、也不改变测试或
用户账号。

人工验证时：在后台给黄金宝箱某一奖励勾选“世界播报”并保存，使用拥有对应宝箱和钥匙
的角色开箱；当前在线客户端应在世界频道看到公告，新登录客户端应从最近 30 条历史中
收到同一条消息。未勾选的奖励则不产生公告。
