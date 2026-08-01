# 宝箱奖励后台可配（金/银/铜）

Date: 2026-07-28

Status: implemented (server + admin)

```text
phase: 1/7/15 open -> roll from MySQL server_chest_rewards (weighted)
       gold 524 falls back to legacy rule pool if DB empty
trigger: 需要在管理后台动态配置黄金/白银/青铜宝箱奖励
```

## 行为

| 宝箱 | 钥匙 | 奖励来源 |
|---|---|---|
| 522 青铜 | 813 | MySQL；无启用行则开箱失败 `result=6` |
| 523 白银 | 814 | 同上 |
| 524 黄金 | 815 | MySQL；无启用行时回退旧默认规则 |

表：`server_chest_rewards(chest_item_id, reward_item_id, weight, enabled)`。  
后台：`/?tab=chests`，保存后热加载。可「导入旧黄金默认池」到当前所选宝箱；
支持按**物品/装备分类**、**装备品质**批量启用或禁用当前池内匹配行。

## 修改点

- `mock_server_catalog.c`：池加载/加权抽取；522/523/524 开启
- `web_admin_chests.inc.c` + `web_admin_server.c`：宝箱奖励页
- `server/mysql/migrate_add_chest_rewards.sql`

## 验证

1. `make -j2 server`；执行 migrate（或靠运行时 CREATE TABLE）。
2. 后台打开「宝箱奖励」：黄金可导入默认池；增删改权重立即生效。
3. 白银/青铜：配几条后，持对应箱+钥可开出配置物。
4. 黄金未导入前开箱仍走旧默认池。
