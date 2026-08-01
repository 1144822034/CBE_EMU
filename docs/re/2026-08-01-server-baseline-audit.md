# 服务端基线审计（细锁回退后，2026-08-01）

## 背景

细锁 / 附近玩家实验回退后，稳定基线以 `f83cf40` 前后的 mock-server 源码为准。
随后部分能力（经验卡同倍率规则）已单独补回；2026-08-01 晚间补回批次已将多篇
`docs/re/2026-08-01-*.md` 中描述的缺口合入当前树。本文以**源码核对**为准，供后续排障。

核对范围：`src/server/mock_server_*.c`、`src/main.c`、`src/server/mock-server.c`。

## 总览

| 主题 | 文档宣称 | 代码现状 | 说明 |
| --- | --- | --- | --- |
| 协议锁 + 主战斗态 capture | 有 | **仍在** | operate / 角色·敌方 HP·MP / 奖励 serial 等进 `account_state` |
| 组队 wire 防跨号泄漏 | 有 | **已补回** | `vm_mock_service_clear_request_local_scratch`：restore / request_end / scene_sync_poll / disconnect |
| MySQL 移出协议锁（off_lock flush） | 有 | **仍在** | 锁内拷贝快照，锁外写库；`off_lock=1` |
| 专用 persist worker 异步队列 | 有 | **已补回** | 2 worker / queue 64；`persistDirtySerial` / `persistReflushWanted`；`async_flush=1` |
| 全局缺口进快照（结算限频/结算回复/挂机登录提示等） | 有 | **已补回** | 见下表「已进快照」 |
| `clear_request_local_scratch` | 有 | **已补回** | `mock_server_battle.c`；含 formula_enemy_index / AutoSynth / LastOutcomeChildFlag |
| 仓库写快照 / capture 不冲空壳 / rebind 先 flush | 有 | **已补回** | `warehouse_write_sql(warehouse*)`；条件 capture；同号 offline 才清全局；rebind 先 flush |
| Boss 残血回血每场一次 | 有 | **已补回** | `g_mockBattleMonsterHealUsedSerial`（u32，按 session serial）；进 capture/restore |
| Boss 技能伤害 ×300 | 有 | **仍在** | `CBE_BATTLE_BOSS_SKILL_DAMAGE_PCT` 默认 300 |
| PvE 封魔挡怪技能/回血 | 有 | **已补回** | `resolve_enemy_counter_damage` 读 `silenceRounds`；`mock_battle_enemy_silenced` |
| 敌方 ailment / solo modifier 进快照 | 有 | **已补回** | `mockBattleEnemyAilments[3]` / `mockBattleSoloModifier` 进 account_state |
| 经验卡同倍率 + 8h 上限 | 有 | **仍在（已补回）** | `EXP_CARD_MAX` / `rejected_different_type` / `rejected_max_duration` |
| sticky restore / 跳过干净 roleDb capture | 有 | **已补回** | `g_vm_mock_service_globals_account`；`sticky_restore skipped=1` / `role_db_capture skipped=1` |
| auto/hangup timer session 权威 | 有 | **已补回** | session 字段 + load/store/clear；capture 写 session、restore 从 session 灌入 |
| 战斗热路径砍 operate printf | 有 | **已补回** | operate 主路径仅 `vm_autotest_note`；summary 跳过 `builtin-battle-operate` |
| 细锁 / 窄锁空 poll | 实验 | **已撤销且勿回** | 曾导致附近玩家/传送回归；基线刻意不含 |

## 仍进 `account_capture` / `restore` 的主集（摘要）

仍安全依赖「协议锁 + 整账号灌入」的包括（非穷尽）：

- 战斗：`mockBattleOperate*`、结算/遇敌冷却、角色/敌方 HP 槽；**auto/hangup timer 已迁 session**
- 角色：`roleDb`（sticky 可跳过干净 capture）、`rolePositionDirty`、`roleInventoryDirty`、`warehouse`（条件拷贝）
- 奖励：`battleRewardedSerial` / drops / enemy·role id / recovered serial；`battleRewardRateSuppressedSerial`
- 场景：moveinfo NPC pending、传送石 / 场景切换目标、task transport 等
- 隔离补回：`mockBattleSettleWireRecoverHp/Mp`、`offlinePractiseLoginFlag/Info`、`remoteCompletedSceneTargetSerial`、`mockBattleMonsterHealUsedSerial`、enemy ailments / solo modifier

## 已补回进快照 / 已实现机制

| 全局 / 机制 | 现状 |
| --- | --- |
| `g_mockBattleMonsterHealUsedSerial` | 按 session serial 门闩；capture/restore |
| `g_mockBattleEnemyAilments[3]`、solo/`active_modifier` 镜像 | 进 `account_state` |
| `g_vm_net_mock_battle_reward_rate_suppressed_serial` | `battleRewardRateSuppressedSerial` |
| `g_mockBattleSettleWireRecoverHp/Mp` | 进 capture/restore |
| `g_vm_net_mock_offline_practise_login_flag/info` | 进 capture/restore |
| `g_vm_net_mock_remote_completed_scene_target_serial` | `remoteCompletedSceneTargetSerial` |
| `warehouse_write_sql` 写 job 快照 | 参数 `const vm_net_mock_warehouse_state *warehouse` |
| rebind 先 `account_flush_for_session` | `bind_session_account` 在 mark_offline 前 flush |

## 与异步 MySQL 的关系

当前为文档中的「2 worker 异步队列」：

1. 请求路径 `mark_*_dirty`；
2. CBMR 后 `flush_deferred_role_db`：锁内拷贝 `roleDb`+`warehouse`，`persistGeneration++`，入队；
3. **persist worker** 锁外执行 MySQL（`g_vm_mock_persist_write_mutex`）；`persistDirtySerial` 防清脏过早；busy 再脏则 `persistReflushWanted`。

因此：

- 战斗全局进快照与 async persist **不冲突**（字段多不进 MySQL）。
- 仓库「写 job 快照」与 async persist **已一起做**，避免双丢。
- 验收可看 `persist_workers=2 ... async_flush=1` 与 `role_deferred_flush_queued`。

## 仍刻意不含（勿回）

- 细锁 / 窄锁空 poll / `account_capture_hotpath_lite`
- agent debug 区域（`debug-6d7cbd` 等）

## 相关文档状态

各篇「代码现状」已更新为 **已合入当前树**；冲突时以本文与源码为准。
