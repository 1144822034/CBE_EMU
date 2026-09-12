# 比武 SETTLED 后次日不开场 / 无计时 / 不匹配

日期：2026-09-05  
范围：`src/server/mock_server_arena.c`（Pixi 定时比武）

## 症状

1. 当日比武结束后（`status=SETTLED`），到 00:00 及次日进场窗口，**不会**新建当日场次、也不会清掉上一场的运行态积分视图。
2. 下次比武开放后玩家可进擂台，但顶栏无进场/比赛计时（无有效 `41/5` ENTRY/LIVE HUD）。
3. 到 `matchStartMinute` 不开始匹配。
4. **管理后台「比武 → 保存」后立刻恢复计时与匹配。**

## 根因陈述

| 项 | 内容 |
|----|------|
| 触发条件 | 当日场次已 SETTLED，进程不重启；自然跨到下一开赛日的进场/比赛窗口 |
| 被违反的契约 | 每个 `day_ymd` 应在进场窗口由 `ensure_session` 绑定/新建 session；时钟到点 `reconcile` → ENTRY/LIVE，并驱动 HUD 与 `match_round` |
| 首个错误状态 | `arena_tick` 在 `status==SETTLED` 时**无条件 return**（2026-08-19 为缓解协议锁繁忙引入），导致跨日也无法执行 `ensure_session` / `reconcile` |
| 证据 | 保存配置走 `apply_config_schedule_now` → `reconcile_status_from_desired`，可把卡在 SETTLED 的 runtime 强行推回 ENTRY/LIVE，与「只有保存才好」完全吻合 |
| 次要缺口 | `apply_config_schedule_now` 原先只用 `rt.dayYmd` 回写时钟，跨日保存会把新分钟写到**旧日** session 上，而不是新建今日行 |

`canEnter` 只看配置分钟（`entry_window_open`），不依赖 runtime status，故玩家仍能进场；HUD/匹配依赖 `rt.status∈{ENTRY,LIVE}`，因此表现为「能进场但不计时、不开打」。

清分契约仍是 **进入 ENTRY（或 SETTLED→LIVE）时** `DELETE server_arena_scores`（见 2026-08-19 入场清积分），不是自然日 00:00。跨日若从未进入新 ENTRY，旧场积分就会一直挂在旧 `session_id` 的内存/排行视图上，看起来像「0 点没清记录」。

## 修复

1. `arena_tick`：SETTLED 早退仅保留「**同一 `day_ymd`**」或「尚未进入下一场活动窗口」；跨日且已到 `entryStart..matchEnd+120` 时落日志 `arena_cross_day_reopen`，继续 `ensure_session` + reconcile/match。
2. `apply_config_schedule_now`：活动窗口内先 `ensure_session(今日)`，再 sync/reconcile，避免保存仍绑旧日 session。

同日 SETTLED 后的锁内空转抑制不变，管理后台 503 修复不回退。

## 验证

- [ ] 某日 SETTLED 后不重启进程，次日进场窗口应出现 `arena_cross_day_reopen` + `arena_session_bind`（新 `day_ymd`）
- [ ] 进擂台顶栏出现「比武 进场 …」倒计时；到点「比赛开始」且有 `arena_match` / 积分增长
- [ ] 无需点后台保存即可开场
- [ ] 同日 SETTLED 后管理后台仍应流畅（无持续 `protocol_lock_timeout`）

## 修改点

- `src/server/mock_server_arena.c`：`vm_mock_arena_tick`、`vm_mock_arena_apply_config_schedule_now`
