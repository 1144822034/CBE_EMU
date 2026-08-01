# Auto / hangup timer：session 权威（阶段 D）

日期：2026-08-01

## 代码现状（2026-08-01 补回后）

**已合入当前树。** session 为 auto/hangup timer 唯一权威；`account_state` 同名字段在
capture 前写 session 后清零。详见 `2026-08-01-server-baseline-audit.md`。

| 项 | 现状 |
| --- | --- |
| `session_load/store/clear_auto_timers` | **已实现** |
| `account_clear_auto_timers` | **已实现** |
| capture / restore | restore 末从 session 灌入；capture 前写回 session |
| `session_mark_offline` | 清零 session timer |
| `clear_request_local_scratch` | **已实现**（与阶段 D 配套） |

下文为已落地契约。

## 问题

`g_mockBattleAuto*` / `g_mockHangupLoop*` 曾同时存在于：

1. 进程全局工作区（请求内读写）
2. `vm_mock_service_account_state`（capture/restore 换账号）

在仍串行持协议锁时，账号快照已能隔离多账号；但若继续把 timer 留在 account
子集里，阶段 B「缩小 restore」或将来细锁会再造双源时钟（playback hold 与
prefer-poll-rearm 互相踩）。

## 契约

| 层级 | 职责 |
|------|------|
| `vm_mock_service_client_session` | **唯一权威**：prefer / pending / nextActMs / hangup loop |
| 进程全局 | 请求内 scratch；restore 末从 session 灌入，capture 前写回 session |
| `account_state` 同名字段 | 仅一次性 migrate；绑定 session 后 capture **清零** |
| `vm_mock_service_team` | 仍拥有 HP/MP/round mask/event ring；**不**存 auto timer（每席位 timer 在成员自己的 session） |

离线 / takeover：`session_mark_offline` 清零 session timer，避免下一登录
从 account 脏字段 remigrate 半截 hangup。

## 修改点

- `mock_server_equipment_npc.c`：session 字段 +
  `session_load/store/clear_auto_timers`；capture/restore 改走 session。
- 组队战斗：`prepare_operation` 仍从 `vm_mock_service_team` 灌 HP/mask；
  wire 全局仍由 `clear_request_local_scratch` 在请求边界清空。

## 验证

1. `make -j2`（或本机 server 目标）通过。
2. 单人挂机：`auto_timers_session_load` 在 prefer/hangup 非零时出现；战斗
   playback hold 后下一轮 synth 仍等 `next_act_ms`。
3. 双人同时挂机/自动：A 的 `nextActMs` 不出现在 B 的 load 日志里；B 的 synth
   不被 A 的 hold 挡住。
4. 组队齐射：仍见 `team_battle_round_wait` / acted mask 齐射后再投递。

## 仍未知

- 同账号顶号后是否要续挂机：当前选择 **不续**（offline 清零）。
- 阶段 B 缩小 restore 时，空 poll 可只 `session_load_auto_timers` 而不灌整账号。
