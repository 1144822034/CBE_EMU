# 自动战斗只用普攻：开战清掉了记忆技能

日期：2026-08-01

## 症状

手动放技能后下一场再按 1 开自动，仍普攻。

## 运行时证据

```text
mock_challenge_battle_operate_reset last=1 operate=23 evidence=manual-start-no-prefer
mock_battle_auto11_flag ... last=0 operate=0
mock_battle_auto_synth via=auto-poll operate=0 last=1
```

首场技能 `operate=23` 已被记住；第二场 challenge 在 `prefer=0` 时清掉；
随后 auto11 / poll 只能 `operate=0`。

## 根因

1. challenge start：`prefer=0` 时无条件清 `LastOperate`（误称「不继承技能」）。
2. auto synth 普攻会把 `LastOperate=0` 写回，进一步锁死记忆。

挂机开战从不做 (1)，故挂机能续技能。

## 修改

1. challenge start：**保留**跨场 `LastOperate`（只 `reset_last_operate_target` 清线槽）。
2. `remember_last_operate`：auto synth 的普攻永不写入记忆 operate。

## 验证

1. 手动技能 → 结束 → 再遇怪 → 按 1：`auto_synth operate=skillId+2`（非 0）。
2. 蓝不够仍可 `mp_fallback` 普攻，回蓝后恢复技能。
