# 宝箱奖励后台可配（金/银/铜）

Date: 2026-07-28（2026-08-09：开箱仅按 chest_item_id 抽 DB，去掉黄金旧池回退）

Status: implemented (server + admin)

```text
phase: 1/7/15 open -> roll from MySQL server_chest_rewards
       WHERE chest_item_id = opened box (weighted; pools never mixed)
trigger: 青铜/白银/黄金宝箱各自消耗对应钥匙，并从本箱奖励池抽取
```

## 行为

| 宝箱 | 钥匙 | 奖励来源 |
|---|---|---|
| 522 青铜 | 813 | 仅 `server_chest_rewards.chest_item_id=522`；无启用行则 `result=6` |
| 523 白银 | 814 | 仅 `chest_item_id=523`；无启用行则 `result=6` |
| 524 黄金 | 815 | 仅 `chest_item_id=524`；无启用行则 `result=6`（不再回退旧默认规则） |

表：`server_chest_rewards(chest_item_id, reward_item_id, weight, enabled)`。  
后台：`/?tab=chests`，保存后热加载。可「导入旧黄金默认池」到**当前所选**宝箱（写入该 `chest_item_id` 的行，便于冷启动）；  
支持按**物品/装备分类**、**装备品质**批量启用或禁用当前池内匹配行。

## 修改点

- `mock_server_catalog.c`：池加载/按 `chest_item_id` 加权抽取；522/523/524 + 813/814/815 配对
- `web_admin_chests.inc.c` + `web_admin_server.c`：宝箱奖励页
- `server/mysql/migrate_add_chest_rewards.sql`

## 验证

1. `make -j2`；执行 migrate（或靠运行时 CREATE TABLE）。
2. 后台分别给 522/523/524 配置互不相同的奖励；持对应箱+钥开箱，只出本箱配置物。
3. 用错钥匙（如铜箱+银钥）→ `result=3`。
4. 某箱无启用行 → `result=6`，日志含 `pool=server_chest_rewards chest_item_id=...`。

## 数据清理（192.1.1.3 / 2026-08-09）

开箱逻辑已按 `chest_item_id` 隔离；此前 522/523 与 524 有启用重叠（导入旧池），表现为「铜箱开出金箱页上也有的物」。

已在 `jh_online.server_chest_rewards` 将跨箱重复启用行在**较低** `chest_item_id` 上 `enabled=0`（保留较高箱启用；共 65 行：522→60、523→5）。清理后启用重叠为 0。

直改 MySQL 不会热加载内存池：需重启 `192.1.1.3` 服务端，或在后台宝箱页任意保存一行以触发 reload。
