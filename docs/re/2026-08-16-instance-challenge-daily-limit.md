# 副本向导每日可挑战次数可配置（2026-08-16）

## 契约

- 后台场景设计页「每日进入／挑战规则」新增 **每日可挑战次数**
  （`server_instance_enter_rules.challenge_daily_limit`，0=不限制）。
- 按**目标场景**计数（与传送 `daily_limit` 同 key）：向导配了
  `target_scene` 则记在该场景；纯本地挑战向导则记当前场景。
- 扣次时机：向导「挑战守关怪」真正开战，不是点选项、也不是副本内挂机刷怪。
- 不足：系统消息「挑战次数不足，无法发起副本挑战。」并取消开战。

## 数据

| 表 | 列 |
|----|----|
| `server_instance_enter_rules` | `challenge_daily_limit` |
| `server_instance_enter_daily` | `challenge_count`（与 `enter_count` 分计） |

迁移：`server/mysql/migrate_add_instance_challenge_daily_limit.sql`；
启动 `ensure_tables` 也会 `ALTER` 补列。

## 扣次路径（服务端）

1. `30/10{agree=1}` 后 HAS_FOLLOWUP 首包 `4/10`（CBE 确认流）
2. DirectPending native `4/1`（向导直开）
3. Pixi hangup 流：`30/10{agree=0}` 后约 2s 内、同 scene、同
   `challenge_enemy_id` 的 `4/1`（`instanceChallengeHangupStyleArm`）

**不改 Pixi `GameSession`**：前端仍 `agree=0` 清 pending 再自发 `4/1`；
由路径 3 扣次。

## 修改点

- `mock_server_scene_task.c`：load/save/consume/rollback
- `mock_server_scene_sync.c`：confirm agree=0 开 hangup arm；agree=1 /
  prompt / clear 关 arm
- `mock_server_interaction_login.c`：hangup-style `4/1` 扣次／拒战
- `web_admin_server.c`：表单项 + 保存

## 验证

1. `make -j2`；重启 mock 服务端（无需重打 Pixi 包）。
2. 后台目标场景设挑战次数=1，同角色同日第二次「挑战守关怪」应提示次数不足。
3. 传送次数与挑战次数互不影响。
