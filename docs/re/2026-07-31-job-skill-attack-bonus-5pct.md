# 职业技能统一 +5% 攻击加成（2026-07-31）

## 契约

所有玩家职业**进攻技能**在系数结算后的原始伤害上再乘 `105/100`：

```text
raw = |生命变化| + (力×力系 + 敏×敏系 + 智×智系)/100
raw = raw * (100 + 5) / 100
再经丹药攻击%、抗性/防御、暴击
```

治疗/自身 buff 等非伤害技能不走该路径，不受影响。普攻不乘此加成。

## 修改点

- `mock_server_role.c`：`VM_NET_MOCK_BATTLE_JOB_SKILL_ATTACK_BONUS_PERCENT=5`，
  应用于 `vm_net_mock_battle_player_skill_damage_to_enemy`
- `mock_server_battle.c`：决斗技能伤害同一倍率

与战斗丹药攻击%可叠乘。

## 验证

同一技能、同一属性下伤害约为改前的 1.05 倍（未斩杀、未暴击时）。
`make -j2`
