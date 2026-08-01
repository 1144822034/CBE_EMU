# 挂机自动：蓝不足改普攻

Date: 2026-07-28

Status: implemented (server)

```text
phase: hangup/prefer auto synth operate
trigger: 没蓝仍反复放技能（日志 operate=23 mpcost>rolemp）
```

## 根因

`vm_net_mock_battle_auto_choose_operate` 只复用 `LastOperate`（技能 id+2），
不看 `skill.dsh` 的 `mpCost` 与当前 `g_mockBattleRoleMpCurrent`。
`prepare_skill_mp` 在蓝不足时仍放行并把蓝钳到 0，所以自动会一直「空蓝放技能」。

权威：普攻 `Operate=0`；技能 `Operate=skillId+2`
（`2026-06-25-battle-server-flow.md`）。

## 修改

1. `auto_choose_operate`：技能且 `rolemp < mpcost` → 本回合改 `Operate=0`。
2. `remember_last_operate`：auto synth 因蓝不足落到普攻时，**不**覆盖已记住的技能，
   回蓝后下一 tick 仍优先原技能。

## 验证

1. 挂机打到蓝不够：日志 `mock_battle_auto_mp_fallback ... action=normal-attack`，
   `mock_battle_operate ... operate=0`。
2. 自动回蓝/药后：再出现原技能 `operate=skillId+2`。
