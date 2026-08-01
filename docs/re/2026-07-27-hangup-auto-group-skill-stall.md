# 自动挂机群攻技能卡住（人物不动作）

Date: 2026-07-27

Status: implemented (server)

```text
phase: hangup/challenge start prefer -> client continuous 4/2 (last group skill)
       OR mid-fight prefer + turn-gap 4/2
trigger: 自动挂机（或开战已 prefer）使用群攻；人物站立无攻击动画
```

## 根因摘要

1. **首个偏离**：`4/2` 操作请求被应答为 **仅有 `4/11 type=1`**（`turn_gap_wait`），
   没有 `4/6 actioninfo`。客户端（尤其群体技能）在等战斗动作结果，表现为人物不动作。
2. **触发链**：
   - 挂机/开战 `arm_pending` 把 `NextActNotBeforeMs` 设成 now+3s（取消窗）；
   - 开战包已带 `4/11`，客户端进入连续 `4/2`，立刻重放上次群攻；
   - `synchronized_team_battle` 在 `in_turn_gap` 时对真实 `4/2` 回 `turn_gap_wait`。
3. **为何偏群攻**：已有负向证据「`4/6` 同包夹带 `4/11` → AOE net-wait stall」；
   对挂起的群攻 `4/2` 只回 `4/11` 同属破坏 action 契约。普攻路径有时不那么显眼。
4. **非根因（已排除/次要）**：`actionInfo` 128→512 溢出、目标指向=4 结算——那是手动多怪
   群攻空应答问题；本反馈是 prefer/挂机自动路径上的 **应答类型错误**。

## 修复

1. **取消窗只挡 poll 合成**：`pending solo auto` 仍尊重 `in_turn_gap`；真实 `4/2`
   一律走 operate → `4/6`，删除 `turn_gap_wait`。
2. **开战/旗标 prefer `arm_pending`**：开战挂机/跨场 prefer 开
   `cancel_window=1`（默认 5s），只挡 **poll synth**；真实 `4/2` 仍走 `4/6`
   （见 `2026-07-28-auto-button-cancel-hangup.md`）。中途 `prefer-poll-rearm`
   在 hold 已到期时仍可为 `cancel_window=0`。
3. 更新 `2026-07-25-battle-auto-flag-stall.md` 契约第 8 条。

## 验证

- [x] `make -j2 server`（或全量 `make -j2`）
- [ ] 挂机自动 + 上次技能为群攻：首击应见 `mock_battle_operate ... target_mode=4` 或
      `mock_battle_auto_synth`，**无** `mock_battle_auto_turn_gap_wait`
- [ ] 人物播放群攻/普攻动画，多怪可连续；`4/11 type=0` 仍能关自动
- [ ] 出手后 ~3s 内 poll 不合成；真实连续 `4/2` 仍应得 `4/6` 而非卡死
