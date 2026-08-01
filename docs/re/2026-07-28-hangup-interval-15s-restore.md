# 挂机 15s→8s/10s 后体感「出手即下场」

Date: 2026-07-28

Status: mitigated (timers restored)

```text
phase: auto hangup operate -> (optional counter) -> victory/settlement
       -> hangup loop re-entry
trigger: 用户认为 15s 改 8s 后，自动挂机出现「玩家行动后怪没行动就跳出」
```

## 两个被缩短的计时器

| 开关 | 曾改为 | 现默认 | 作用层 |
|------|--------|--------|--------|
| `CBE_BATTLE_REWARD_MIN_INTERVAL_MS` | 8000 | **15000**（后于同日 pacing-refactor 再下调为 **8000**） | 仅结算发奖；不拦开战、不跳过怪回合 |
| `CBE_HANGUP_LOOP_INTERVAL_MS` | 10000 | **15000**（后于同日 pacing-refactor 再下调为 **8000**，且改为结算离场后再计时） | 地图侧再投下一场挂机开战 |

奖励闸门本身不会让本场省略怪反击。更可能是短间隔连场 + 秒杀/群攻全灭（本包无 counter）叠在一起，体感成「出手后怪没动就跳出」。

## 修改

- 奖励默认间隔曾恢复 15000；当前默认见 `2026-07-28-hangup-loop-pacing-refactor.md`（**8000**）。
- 挂机循环默认间隔曾恢复 15000；当前默认 **8000**，且 timer 在结算离场后起算。

## 验证

1. 重启 `jh-online-server`。
2. 挂机：离场后日志 `mock_hangup_loop_schedule ... interval_ms=8000`（当前默认；本篇曾验证 15000）。
3. 回地图后约 15s 才见 `mock_hangup_loop_poll_deliver`。
4. 若仍出现「未全灭却无 counter / 直接 note_victory」，贴 `mock_battle_operate ... slots=... counters=...` 一行再查多怪胜利契约。
