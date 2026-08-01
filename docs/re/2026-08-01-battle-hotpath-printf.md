# 战斗热路径：砍 operate/deliver printf + 跳过 summary

日期：2026-08-01

## 代码现状（2026-08-01 审计）

**热路径 info printf 砍削不在当前树。** `mock_battle_operate` /
`team_battle_deliver` / `team_battle_action_deliver` 仍打 info 行。传输层仅对
`builtin-actor-moveinfo-ack` 跳过 summary+`fflush`，**未**对
`builtin-battle-operate` 跳过。细锁仍未做。详见
`2026-08-01-server-baseline-audit.md`。

下文记录当时取证与曾合入后的 post-fix 数字；与当前源码不一致时以源码为准。

## 触发与取证

双端组队挂机 + 组队战斗。NDJSON session `6d7cbd`（pre-fix）：

| 指标 | 结果 |
|------|------|
| `hot_printf`（operate/team_deliver/action_deliver） | 22 次，`printf_ms` 全为 0（1ms 分辨率） |
| `4/2` `hold_ms` | n=18，avg≈3.8，max=21 |
| `wait_ms` | 30/30 为 0 |
| `summary_fflush_ms`（解锁后） | avg≈0.3，max=1 |

## 假设

1. **持锁长 printf 是 hold 主因** — **否定**（单次测到的 info 行均为 0ms）。`hold`/`process` 主要来自战斗组包本身。
2. **解锁后 summary+fflush 有可见开销** — **弱确认**（亚毫秒～1ms；挂机时 4/2 很密）。
3. **`wait_ms` 偏高需细锁** — **否定**（本轮全 0）。session/presence 窄锁延后；sticky moveinfo/空 poll 仍碰账号域时细锁 ROI 低。

## 修改

- 删除热路径 **info** `printf`：`mock_battle_operate`、`team_battle_deliver`、`team_battle_action_deliver`（保留 warn/error 与 `vm_autotest_note`）。
- 传输层对 `builtin-battle-operate` 与 moveinfo 一样跳过每请求 summary + `fflush`。
- **未做**细锁（见上）。

## 验证（post-fix，session `6d7cbd`）

| 指标 | pre-fix | post-fix |
|------|----------|-----------|
| operate `skip_summary` | 无 | 12/12 = 1 |
| operate `summary_fflush_ms` | avg≈0.33 | 12/12 = 0 |
| operate `hold_ms` avg | ≈3.83 | ≈4.08（几乎不变，符合预期） |
| `wait_ms` | 30/30 = 0 | 19/20 = 0（1 次 = 1） |
| 热路径 info printf | 仍打印 | 已删除（探针 `cut=1` 22/22） |

细锁未做：`wait_ms` 不构成瓶颈。调试探针已移除。
