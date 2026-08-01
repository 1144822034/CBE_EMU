# 多人协议锁热路径：延后落库 + 空 poll 快路径

日期：2026-07-30

## 问题

多人同服时客户端卡顿。根因不是 WT 组包 CPU，而是：

1. 全局 `g_vm_mock_service_protocol_mutex` 内同步 `role_db_save`（背包 DELETE+INSERT）
2. 每个 scene-sync poll 都扫全 session 做 `expire_stale`（偶发再 flush MySQL）
3. 空 poll 仍走附近玩家 seed 扫描，并在无 payload 时 `fflush(stdout)`

单人 `process_ms` 常只有 0–5ms，但一人慢写库会抬高其他人的 `state_wait_ms`。

## 修改

### A1 热路径延后落库

- 新增 `vm_net_mock_role_mark_inventory_dirty(reason)`：只置脏，首置时打一行日志。
- 战斗结算/技能/用药、挂机路径 `battle-state`、商店 buy14、穿脱/强化、仓库存取、
  背包 add、任务提交/放弃、开箱成功、称号等热路径改为 mark dirty。
- **仍同步写库**：登录选角/建号、admin 加减币、rollback、付费传送扣元宝、
  offline practise settle、断线/心跳 expire flush。
- 传输层统一 `vm_mock_service_flush_deferred_role_db`：CBMR 发出后再 flush；
  **poll 路径同样 flush**（挂机 settle 常在 poll 内 mark dirty）。

风险：进程在 CBMR 之后、flush 之前崩溃，可能丢掉最后一次内存变更（与丢弃延后落库相同）。

### A2 空 poll / expire

- `expire_stale_online_sessions` 全服最多每 2s 跑一次。
- scene-sync 在无 NPC/任务对象且无附近玩家、无 peerSync/社交/交易/聊天待投递时，
  跳过 `build_scene_role_seeds` 直接返回空。
- 空 poll 不再每次 `fflush(stdout)`（仅非空 poll 打日志时 flush）。

### A3 日志限频

- `mock_actor_moveinfo_ack` 日志最多约 1 行/秒（附带 `suppressed=`）。
- 传输层对 `builtin-actor-moveinfo-ack` 跳过每请求 summary + `fflush`；
  `actor_moveinfo_timing` 仅慢请求（≥50ms）或每秒一条。

### B-lite：协议锁外 MySQL flush

- `role_db_save_relational_ex(account, db*, warehouse*, ...)`：可按快照写库，不依赖
  “当前 restore 是谁”。
- `account_state` 增加 `warehouse` / `persistGeneration` / `persistFlushBusy`。
- `flush_deferred`：短持锁拷贝 `roleDb(+warehouse)` → **解锁** → MySQL → 短持锁按
  generation 清脏。日志带 `off_lock=1`。
- 同账号在 flush 期间又变脏：generation 不匹配则保留 dirty，最多再试一轮。

这样一人挂机 settle 写库时，其他人仍可进协议临界区做 moveinfo/poll 组包。

## 验证

1. `make -j2`（或 `CBE_SERVER_ONLY=1`）通过。
2. 挂机一轮：应出现 `mock_role_persist_deferred` 与
   `role_deferred_flush ... off_lock=1 flush_ms=...`。
3. 多人同图：一人挂机时其他人 `state_wait_ms` 不应再跟 MySQL `flush_ms` 同量级爬升。
4. 仓库存取后断线重登：仓库与背包与操作后一致。
5. 连续走动：moveinfo 日志密度明显下降，stdout 重定向时卡顿减轻。

## 未做（后续）

- 真并行组包（per-request context，去掉 restore/capture 全局态）
- moveinfo/poll 与账号业务拆细锁
- flush 完全异步队列（当前仍在请求线程同步等 MySQL，只是不占协议锁）
