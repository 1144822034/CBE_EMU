# 2026-07-29 战斗命中 / 闪躲 / 暴击 / 抗性

## 触发与首次偏离

玩家属性模型已派生 `hit` / `dodge` / `crit` / `resist`，装备与强化也会放大对应
`equip.dsh` 列，技能时效 buff 也能改这四项。但战斗伤害路径只使用攻击与防御（以及
技能力/敏/智系数），从未掷骰，法术也不读抗性。

首次偏离在 `vm_net_mock_battle_player_damage_to_enemy` /
`vm_net_mock_battle_player_skill_damage_to_enemy` /
`vm_net_mock_battle_enemy_damage_to_role`（以及切磋同构路径）：属性算出来了却不参与结算。

## 证据边界

- 已确认：属性派生公式见 `docs/re/2026-06-28-player-attribute-model.md`；`actioninfo`
  type-1 child 允许 `valueA=0`（群体增益已用）；未命中用 `valueA=0` 不扣血可推进动作队列。
- **unresolved**：原始客户端精确命中率、暴击倍率、抗性曲线表尚未从 CBE/CBM 或权威抓包恢复。
  本实现为 provisional，需后续用真服包或 IDA 战斗结算交叉验证。

## Provisional 契约

```text
hit/dodge/crit/resist (player) = equipment columns only（含强化附加）
hitChance = clamp(attacker.hit - defender.dodge, 5, 95)
critChance = clamp(attacker.crit, 0, 100)
critDamage = round(damage * 1.5)   # 至少 +1
physical: damage_after_defense(raw, defense)
spell:    damage = raw * (100 - min(resist, 70)) / 100
```

法术判定：`skill.dsh` 中 `智慧系数` 非零且 ≥ `力量系数`、`敏捷系数`（如绯炎幻法）。
玩家确认：**鬼道法术伤害忽略闪躲**——智慧主导进攻技能不做命中/闪躲掷骰，仍受抗性减免与暴击影响；普攻与物技照常检定闪躲。

怪物次级属性（暂无 `automonster` 列；抗性同样按百分比、上限 70% 减免）：

```text
hit = 70 + level*2
dodge = 2 + level/2
crit = 1 + level/10
resist = level
```

2026-07-29 调整：玩家命中/闪避/暴击/抗性不再叠等级与三围底值；抗性从防御同构软曲线改为百分比减伤，最多 70%。
未命中返回伤害 `0`；`actioninfo` 仍写目标 child，`valueA=0`，且
`child_flag=3`。暴击在放大数值的同时写 `child_flag=2`。

### child_flag 显示契约（已对齐 CBM）

`mmBattleMstarWqvga.cbm` VA `0x24f6`（调用点 `0x504c` / `0x511c` / `0x53fe`）：

```text
ldrb r0, [child_slot+2]   ; child_flag
bl   0x24f6
  cmp flag,#2 → strcpy 「暴击」
  cmp flag,#3 → strcpy 「闪躲」
  else no-op（保留数值飘字）
```

ADR：`0x2506`→「闪躲」，`0x250c`→「暴击」（同编码不同 PC）。

因此未命中不能只靠 `valueA=0`（界面会显示数字 0）；必须带 `child_flag=3`
才会提示「闪躲」。增益/封魔等 `supportNoDamage` 且 `valueA=0` 的路径保持
`child_flag=0`，避免误显示闪躲。

命中后若防御/抗性把伤害压到 0，仍保底为 `1`（破防）；**闪避/未命中不改为 1**
（2026-07-29 用户确认）。

## 环境开关

| 变量 | 作用 |
| --- | --- |
| `CBE_BATTLE_DISABLE_COMBAT_ROLLS=1` | 关闭掷骰（恒命中、无暴击） |
| `CBE_BATTLE_FORCE_HIT=1` | 强制命中 |
| `CBE_BATTLE_FORCE_MISS=1` | 强制未命中（应飘「闪躲」） |
| `CBE_BATTLE_FORCE_CRIT=0/1` | 永不暴击 / 强制暴击（未设置则按暴击率掷；暴击应飘「暴击」） |
| `CBE_BATTLE_FIRST_CHILD_FLAG` | 若设置则覆盖进攻 child_flag（调试） |
| `CBE_BATTLE_COUNTER_CHILD_FLAG` | 若设置则覆盖反击 child_flag（调试） |
| `CBE_BATTLE_SKILL_ENEMY_RESIST` | 覆盖法术抗性减免 |

## 修改点

- `mock_server_role.c`：掷骰与减免助手；miss→`child_flag=3`、crit→`child_flag=2`。
- `mock_server_battle.c`：actioninfo 写入 outcome child_flag；切磋同步。
- `mock_server_core.c`：`apply_damage_to_role(0)` 不再强行改成 1。

## 验证

- `make server -j2`
- 手动：`FORCE_MISS=1` 应显示「闪躲」而非数字 `0`；`FORCE_CRIT=1` 应显示「暴击」；
  增益技能 `valueA=0` 不得误显闪躲。
