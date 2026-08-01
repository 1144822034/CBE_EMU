# Mock Service 进程全局变量与多玩家隔离

日期：2026-08-01

## 代码现状（2026-08-01 补回后）

**已合入当前树。** 详见 `2026-08-01-server-baseline-audit.md`。

| 项 | 现状 |
| --- | --- |
| 主战斗 operate / HP·MP / 奖励 serial 进 `account_state` | 仍在 |
| `vm_mock_service_clear_request_local_scratch` | **已存在**（restore / request_end / poll / disconnect） |
| reward suppressed / settle recover / offline practise / remote scene serial | **已进** capture/restore |
| 敌方 ailment / solo modifier 进快照 | **已进** |
| Boss `MonsterHealUsed` | **已进**（`mockBattleMonsterHealUsedSerial`，u32 serial 门闩） |

下文「修改 / 验证」描述的目标契约**已在当前树**。

## 背景

Mock service 仍以「协议锁 + 账号 restore/capture」隔离多账号，大量业务字段是
进程全局工作区，请求进入时从 `vm_mock_service_account_state` 灌入，结束时写回。
同账号多端共享一份 `account_state` 是既有契约。

## 分类

### 可共享（只读目录 / 基础设施）

- 商店 / 装备 / 技能 / 任务 / 怪物 / 登录服等 catalog
- schema / DB ready 标志、账号与好友 DB 缓存
- `g_vm_mock_service_protocol_mutex`、worker pool
- 组队 / 切磋 / 交易表（按 `clientId` 索引的共享对象）

### 已进账号快照（多账号安全，依赖锁 + capture 完整）

- 战斗 operate / HP·MP / 挂机全局镜像（**不含**敌方 ailment / solo modifier）
- 角色 DB、仓库整份拷贝、position/inventory dirty
- 传送石 / 场景切换 / moveinfo NPC pending
- 结算奖励 serial / drops / enemy·role id

### 本轮补进快照的缺口（此前会串账号）

| 全局 | 风险 |
|------|------|
| `g_vm_net_mock_battle_reward_rate_suppressed_serial` | A 的限频结算可压掉 B 的金币 |
| `g_mockBattleSettleWireRecoverHp/Mp` | 结算回复量泄漏 |
| `g_vm_net_mock_offline_practise_login_flag/info` | B 登录看到 A 的挂机提示 |
| `g_vm_net_mock_remote_completed_scene_target_serial` | 场景完成 serial 串线 |

### 请求局部（不应进快照，必须在边界清空）

| 全局 | 说明 |
|------|------|
| `g_vm_net_mock_team_battle_*` 等组队 wire | 已证可导致单人技能自伤（见 `2026-07-26-battle-skill-self-hit-guard.md`） |
| `g_vm_net_mock_battle_formula_enemy_index` | 公式敌方槽 |
| `g_mockBattleAutoSynthInProgress` | 自动合成 reentrancy |
| `g_mockBattleLastOutcomeChildFlag` | 下一发 actioninfo 的 crit/dodge 标记 |

### 双源状态

- `g_mockBattleAwaitsRevivalConfirm`（账号快照）与
  `session->awaitsBattleRevivalConfirm`（连接）
- capture 取 OR；restore 在已绑定 `active_client_id` 时回写 session

## 修改

1. 上述缺口字段加入 `vm_mock_service_account_state` 的 capture/restore。
2. 抽出 `vm_mock_service_clear_request_local_scratch`：在 `account_restore` 与
   每条协议请求结束（主路径 / scene-sync poll / disconnect）清空组队 wire 等。
3. 主路径与 poll/disconnect 在 restore 前设置 `active_client_id`，复活确认可对齐 session。
4. `g_mockBattleAutoSynthInProgress` 上移到 `mock_server_core.c`，与其它 wire 局部状态同层。

## 验证

1. `make -j2`
2. 双账号：A 组队打怪后 B 单人放技能，不应再出现 `party_count` 泄漏导致的自伤线槽；
   若上一请求留下 party，日志为 `team_battle_context_clear reason=...`。
3. A 触发结算限频后 B 正常战斗结算，金币/掉落不应被 A 的 suppressed serial 清零。
4. A 离线挂机横幅不应出现在 B 的登录包。

## 仍未知 / 后续

- 长期方向仍是去掉全局工作区，改为显式 per-request account context（见
  `2026-07-20-mock-service-concurrency.md`）。
- `g_vmNetMockFollowupResponse*` 已无业务写入，可后续删除。
- 登录 issue username/password 仍是进程 scratch，当前每请求清零，风险较低。
