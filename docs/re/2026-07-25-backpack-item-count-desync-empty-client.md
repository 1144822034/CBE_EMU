# 背包库表有行但重登仍空（backpack_item_count 不同步）

## 触发与现象

- 客户端背包“已用 = 0 / 物品消失”。
- `account_role_backpack` 仍有该角色的物品行。
- 玩家重新登录后界面仍不恢复。

## 根因

关系型加载路径：

1. `account_roles.backpack_item_count` 写入内存 `role->backpackItemCount`；
2. `account_role_backpack` 按 `slot_index` 写入 `role->backpackItems[]`；
3. 随后 `vm_net_mock_role_normalize_backpack()` 只扫描 `[0, backpackItemCount)`，
   再 `memset` 整表并以压缩结果覆盖。

当缓存计数为 `0`（或小于实际占用槽位）时，刚从库表载入的物品在 normalize
阶段被整表丢掉。本次登录会话内存背包为空，故 `30/21` / `17/1` 都没有可下发
的通用网格行；又因纯 MySQL 加载路径不一定立刻回写，库表行可以继续残留，
形成“库里有、客户端永远空”的稳定假象。

首次偏离点：normalize 把 `backpack_item_count` 当成槽位扫描上界，而权威数据
在 `account_role_backpack` 的占用槽。

## 修复

`normalize_backpack` 改为扫描全部 `BACKPACK_MAX_ITEMS` 槽位中的非空行，再按
容量压缩；若声明计数与实际占用不一致则打
`mock_backpack_count_resync` 取证日志，并把 `backpackItemCount` 纠正为压缩后
的真实长度。

## 验证

1. 对受影响账号执行：

   ```sql
   SELECT role_id, backpack_capacity, backpack_item_count
   FROM account_roles WHERE account_id=...;
   SELECT role_id, slot_index, item_id, item_seq, item_count
   FROM account_role_backpack WHERE account_id=... AND role_id=...
   ORDER BY slot_index;
   ```

   典型病灶：`backpack_item_count = 0`（或明显小于实际行数），但背包表有行。

2. 部署修复后重新登录：日志出现 `mock_backpack_count_resync ... occupied>0 kept>0`，
   随后 `mock_backpack_grid` / `mock_backpack_items` 的 `rows/gridnum > 0`，
   客户端已用槽恢复。

3. 正常计数一致的角色不应出现 resync 警告，背包行为不变。
