# 自动战斗出手与挂机对齐

日期：2026-08-01

## 需求

战内自动释放技能逻辑与挂机相同；唯一区别是进战方式：

- 自动：玩家手动遇怪（`4/1`）
- 挂机：系统定时拉入战斗

## 根因

选技/`auto_choose_operate` 本已共用，但中途按 1（`4/11 type=1`）走
**内联 operate-only**，并再 `flag_poll` 补 `4/11`；挂机开战则是 start 带
`type=1` + poll synth。两条路径体感与时序不一致。

## 修改

1. `auto11 type=1`：`prefer` + `HangupStyleFlagOk` + arm pending，只 ACK `4/11`
2. `auto12`：不再内联 synth；空 ACK + 保 pending
3. 出手仍只经 scene-poll → `auto_choose_operate`（记忆技能 / 蓝不够普攻）

## 验证

1. 手动放技能 → 按 1 → 日志 `hangup-style-arm`，随后 `auto-poll` 放同一技能
2. 挂机连打技能行为不变
3. 蓝不够：`mp_fallback`；回蓝后恢复技能
