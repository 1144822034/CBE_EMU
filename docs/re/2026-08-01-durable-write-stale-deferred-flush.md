# 同步落盘与延后 flush / sticky capture 冲突（2026-08-01）

## 现象

1. 传送成功后重登，角色回到传送前 `scene/pos`。
2. 传送后再进出商店，`shop_return` 的 deferred `30/1` 把人拉回传送前坐标。
3. 交易成交后（尤其任一方刚有战斗掉落等 dirty 背包时）物品/金钱被打回成交前。

## 业务链路

```text
mark_inventory_dirty / 部分路径
  → CBMR 后 flush_deferred：拷贝 account.roleDb 快照，persistGeneration++，入队
同步权威写：
  - 传送/进图：role_set_position → role_db_save（整包）
  - 走路：role_commit_timeline_position（窄 UPDATE pos）
  - 交易：trade_persist_pair（双方 money+backpack）
  - 仓库：role_db_save（背包+仓库同事务）
读路径：
  - 重登：MySQL account_roles / backpack
  - 出商店：内存 role->x/y（trade_role_for_session）→ 再 save_player_pos_state
```

## 根因陈述

- **触发**：传送/交易/timeline 等同步写库成功时，进程内仍可能挂着更早入队的 deferred job；且 sticky capture 在 `!dirty` 时跳过 `roleDb` memcpy。
- **被违反的契约**：一旦出现更新的耐久权威（已 COMMIT 的 MySQL 状态），任何更旧的 role 快照不得再写同一账号，账号堆镜像也不得继续持有写前坐标/背包。
- **首个错误状态**：
  1. 旧 deferred job 仍持有写前 `persistGeneration`，worker 把它写回 MySQL；或
  2. `account_state.roleDb` 停在写前快照，账号切换 restore / shop-return 读到旧坐标。
- **证据**：
  - `role_set_position` / `trade_persist_pair` 成功路径原先不 bump `persistGeneration`。
  - sticky：`role_db_capture skipped=1` 依赖 dirty 位；同步 `role_db_save` 会清 dirty 却不刷新 `account.roleDb`。
  - 出商店：`shop_return_kind2_reenter_arm` 读内存 `role->x/y`，旧镜像会把旧点再次 `save_player_pos_state`。
- **排除**：不是客户端传送协议未完成；也不是交易 SQL 漏写（`trade_commit` 后 MySQL 曾正确，随后被旧 flush 覆盖）。

## 修改

新增 `vm_mock_service_account_note_durable_role_write`：

1. 刷新 `account_state.roleDb`（及条件 warehouse）。
2. `persistGeneration++`，使在途 deferred job `role_deferred_flush_superseded`。
3. 按提交范围清 `roleInventoryDirty` / `rolePositionDirty`；若仍有脏位则 `persistDirtySerial++` 并在 busy 时 `persistReflushWanted`（timeline 只提交坐标时保留背包延后写）。

调用点：

| 路径 | inv | pos |
| --- | --- | --- |
| 同步 `role_db_save`（`clear_process_dirty_flags`） | 是 | 是 |
| `role_commit_timeline_position` | 否 | 是 |
| `trade_commit` 双方 | 是 | 否 |

## 验证

1. `make -j2`
2. 战斗掉落后立即传送，再重登：坐标为目标点；日志可有 `durable_role_write_invalidate` + 可选 `role_deferred_flush_superseded`。
3. 同上后进出商店：`shop_return_kind2_reenter_arm` 的 `pos` 为目标点，不是传送前。
4. 掉落后立即交易并重登：双方背包与成交结果一致，不被旧 flush 打回。
5. 仅走路 timeline：坐标即时落库；若同时有背包 dirty，仍应出现后续 `role_deferred_flush`（reflush），且坐标不被打回。
