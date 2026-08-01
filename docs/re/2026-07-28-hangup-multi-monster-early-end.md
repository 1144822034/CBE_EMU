# 挂机多怪未死完就结束战斗

Date: 2026-07-28

Status: implemented (server)

```text
phase: hangup/auto multi-monster → rapid synth → settle while models still up
       OR challenge start aborts pending settlement_exit
trigger: 挂机/自动多怪战，客户端仍见存活怪但已进结算或下场
```

## 根因摘要

1. **账号 restore 抹掉 playback hold**  
   `account_save` 只持久化 `prefer` / hangup loop，restore 故意把
   `NextActNotBeforeMs` / `PendingArmed` 清零。下一拍 scene-poll 的
   `prefer-poll-rearm ... not_before_ms=0` 立刻 synth 下一刀。  
   日志：`playback_hold not_before=43465` → 同场稍后
   `prefer-poll-rearm not_before=0` → 连刀至 `victory`，客户端 actioninfo
   未播完，表现为「怪还没死完就结束」。

2. **结算未拆完又开战**  
   日志：`settlement_exit_arm` 后 `settlement_exit_clear reason=hangup-battle-start`
   + `challenge_battle_start enemies=3`。挑战/挂机开战会清
   `AwaitingSettlement` 并重置 slot，打断上一场 `4/8` 离场。

权威：`mmBattle` actioninfo 播放；`account_restore` 跨请求会话；多怪
slot 胜利契约见 `2026-07-27-multi-monster-early-exit.md`。

## 修改

1. 账号态持久化并恢复：`PendingArmed` / `NextActNotBeforeMs` /
   `PendingNotBeforeTick` / flag pending / `LastRoundActionCount` /
   `ClientDriven` / `SuppressNext12`。
2. `arm_pending_after_act`：不再先把 hold 抹零；保留
   `max(已有 hold, 新 gap)`。
3. `seat_can_act` / synth 再武装：用 `all_enemies_defeated()`，不用裸
   `EnemyHpCurrent`。
4. auto 选目标：不以 `live==0` 表示失败（wire 0 合法）。
5. challenge / hangup 开战：若仍在结算/exit pending，投递 exit 或 hold
   panel，禁止 `settlement_exit_clear` 式重开。

## 验证

1. 多怪自动：`playback_hold` 后同账号下一请求不应再出现
   `prefer-poll-rearm ... not_before_ms=0`（应保持 pending 直到 hold 到期）。
2. 三怪需三刀全灭后才 `mock_battle_settle`；中间刀无 victory。
3. 结算 exit 武装期间点下只怪：日志
   `mock_battle_start_blocked_by_settle`，先 `settlement_exit`，不
   `hangup-battle-start` 清 pending。
4. `make -j2 server`。

## 相关

- `2026-07-27-multi-monster-early-exit.md`
- `2026-07-28-battle-auto-stomp-counter-playback.md`
- `2026-07-28-hangup-settlement-blank-shell.md`
