# 服务端基线审计（细锁回退后，2026-08-01）

## 背景

细锁 / 附近玩家实验回退后，稳定基线以 `f83cf40` 前后的 mock-server 源码为准。
随后部分能力（经验卡同倍率规则）已单独补回，但多篇 `docs/re/2026-08-01-*.md`
仍按「已落地」描述，与当前树不一致。本文以**源码核对**为准，供后续补回与排障。

核对范围：`src/server/mock_server_*.c`、`src/main.c`、`src/server/mock-server.c`。

## 总览

| 主题 | 文档宣称 | 代码现状 | 说明 |
| --- | --- | --- | --- |
| 协议锁 + 主战斗态 capture | 有 | **仍在** | operate / 角色·敌方 HP·MP / 挂机全局 / 奖励 serial 等进 `account_state` |
| 组队 wire 防跨号泄漏 | 有 | **仍在** | `account_restore` 清 `team_battle_*`（`team_battle_context_clear_on_account_restore`） |
| MySQL 移出协议锁（off_lock flush） | 有 | **仍在** | `flush_deferred_role_db` 拷贝快照后锁外 `save_relational_ex`，日志 `off_lock=1` |
| 专用 persist worker 异步队列 | 有 | **不在** | 无 `persist_workers` / `persistDirtySerial` / `persistReflushWanted`；仍在 game worker 上等 `flush_ms` |
| 全局缺口进快照（结算限频/结算回复/挂机登录提示等） | 有 | **不在** | 见下表「未进快照」 |
| `clear_request_local_scratch` | 有 | **不在** | 仅 restore 时清组队 wire，无统一边界清理函数 |
| 仓库写快照 / capture 不冲空壳 / rebind 先 flush | 有 | **不在** | `warehouse_write_sql` 仍读 `g_vm_net_mock_warehouse`；capture 无条件覆盖；`title-login-rebind` 不先 flush |
| Boss 残血回血每场一次 | 有 | **仍在（简化）** | `g_mockBattleMonsterHealUsed` bool + 开战清零；**未**进 capture |
| Boss 技能伤害 ×300 | 有 | **仍在** | `CBE_BATTLE_BOSS_SKILL_DAMAGE_PCT` 默认 300 |
| PvE 封魔挡怪技能/回血 | 有 | **不在** | `resolve_enemy_counter_damage` 不读 `silenceRounds`；无 `mock_battle_enemy_silenced` |
| 敌方 ailment / solo modifier 进快照 | 有 | **不在** | `g_mockBattleEnemyAilments` 为进程全局 |
| 经验卡同倍率 + 8h 上限 | 有 | **仍在（已补回）** | `EXP_CARD_MAX` / `rejected_different_type` / `rejected_max_duration` |
| sticky restore / 跳过干净 roleDb capture | 有 | **不在** | 无 `sticky_restore` / `role_db_capture skipped` |
| auto/hangup timer session 权威 | 有 | **不在** | timer 仍在 `account_state` + 全局，随 capture/restore |
| 战斗热路径砍 operate printf | 有 | **不在** | `mock_battle_operate` / `team_battle_*_deliver` 仍有 info printf；summary 仅跳过 moveinfo |
| 细锁 / 窄锁空 poll | 实验 | **已撤销且勿回** | 曾导致附近玩家/传送回归；基线刻意不含 |

## 仍进 `account_capture` / `restore` 的主集（摘要）

仍安全依赖「协议锁 + 整账号灌入」的包括（非穷尽）：

- 战斗：`mockBattleOperate*`、结算/遇敌冷却、auto/hangup **全局镜像**、角色/敌方 HP 槽
- 角色：`roleDb`、`rolePositionDirty`、`roleInventoryDirty`、`warehouse`（整份拷贝）
- 奖励：`battleRewardedSerial` / drops / enemy·role id / recovered serial
- 场景：moveinfo NPC pending、传送石 / 场景切换目标、task transport 等

## 文档写了但未进快照 / 未实现的缺口

| 全局 / 机制 | 风险（多账号） |
| --- | --- |
| `g_mockBattleMonsterHealUsed` | A 回血后门闩可能影响 B 同进程战斗 |
| `g_mockBattleEnemyAilments[3]`、solo/`active_modifier` 持久镜像 | 封魔/减益可串号 |
| `g_vm_net_mock_battle_reward_rate_suppressed_serial` | A 结算限频可压掉 B 金币 |
| `g_mockBattleSettleWireRecoverHp/Mp` | 结算回复量泄漏 |
| `g_vm_net_mock_offline_practise_login_flag/info` | B 登录见 A 挂机提示 |
| `g_vm_net_mock_remote_completed_scene_target_serial`（若仍存在） | 场景完成 serial 串线 |
| `warehouse_write_sql` 读全局 | 与 **off_lock flush** 叠加 → 存仓后双丢（包删仓未写） |
| rebind 不先 `account_flush_for_session` | 回标题丢失未刷脏库存 |

## 与异步 MySQL 的关系

当前**不是**文档中的「2 worker 异步队列」，而是：

1. 请求路径 `mark_*_dirty`；
2. CBMR 后 `flush_deferred_role_db`：锁内拷贝 `roleDb`+`warehouse`，`persistGeneration++`；
3. **同一 game worker** 上锁外执行 MySQL（`off_lock=1`）。

因此：

- 补「战斗全局进快照」与现有 off_lock flush **不冲突**（字段多不进 MySQL）。
- 补仓库「写 job 快照」与 off_lock flush **必须一起做**，否则异步/掉锁写库会放大双丢。
- 文档 `2026-08-01-async-role-persist-queue.md` 中的 persist worker 方案视为**未合入或已撤回**，勿按 `async_flush=1` / `persist_workers=2` 验收。

## 建议补回优先级（不涉及细锁）

1. **仓库**：`warehouse_write_sql(warehouse*)` + capture 条件覆盖 + rebind 前 flush  
2. **隔离缺口**：reward suppressed / settle recover / offline practise / heal-used / enemy ailments 进 `account_state`  
3. **PvE 封魔**：`resolve_enemy_counter_damage` 读 `silenceRounds`  
4. （可选）persist worker 队列——独立性能项，与 1–3 无硬依赖  

## 相关文档状态

各篇已加「代码现状」节或指向本文；冲突时以本文与源码为准。
