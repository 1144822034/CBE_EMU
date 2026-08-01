# 一场战斗后不能再遇怪

Date: 2026-07-28

Status: implemented (server)

```text
phase: victory settle pending -> client 4/1 challenge
trigger: 打完一场后再触怪；不进战斗，只能走路
```

## 压缩结论

| 尝试 | 结果 |
| --- | --- |
| 空 hold `resp=5` 应答 `4/1` | 交互卡住，之后无挑战 |
| 仅 `4/8+4/11+4/9` early-exit `resp=104` 应答 `4/1` | 同样卡住（日志：challenge resp=104 后只有 moveinfo） |
| **正确** | 清掉 settle 门闩，**同一请求**回真正的开战包（`1/4/5` 等） |

`4/1` 的契约是开战，不能用结算离场包顶替。

## 日志证据

1. `hold-panel resp=5` → 满地图 moveinfo、无第二次 challenge。
2. 改 early-exit 后：`builtin-challenge-interaction resp=104`（与
   `settlement_exit ... resp=104` 同长）→ 仍无 `mock_challenge_battle_start`，
   只有 moveinfo。

## 修改

`hold_or_exit_if_settling`：若仍在 `AwaitingSettlement` / exit pending，

1. 打日志 `action=clear-allow-reenter`
2. 清 `Armed` / `AwaitingSettlement` / exit pending
3. **return 0**，让 challenge/hangup builder 继续组开战响应

不再对开战请求返回空包或纯 `4/8` 包。

## 验证

1. 打完一场立刻再点怪：应见 `clear-allow-reenter`，紧接
   `mock_challenge_battle_start`（`resp` 远大于 104，含 battleinfo）。
2. 不应再单独出现 challenge 的 `resp=5` / `resp=104` 而无 battle start。
3. `make -j2 server`。

## 相关

- `2026-07-28-hangup-multi-monster-early-end.md`（曾拦截结算中重开）
- `2026-07-28-multi-monster-empty-settlement.md`
