# 2026-08-03 战斗攻击力公式调整

## 触发

产品约定：普攻攻击力改为全额主属性 + 装备攻击 + 力量，上限 99999。

## 旧公式 → 新公式

```text
旧: attack = 6 + level*2 + jobPrimary/2 + equipment.attack/3  (cap 9999)
新: attack = jobPrimary + equipment.attack + strength         (cap 99999)
```

`jobPrimary` 仍为：天机→力量、幻剑→敏捷、鬼道→智慧。天机因此力量计入两次。

> **2026-08-04 修订**：现行为 `attack = jobPrimary + equipment.attack`（去掉 `+ strength`）。见 `2026-08-04-three-job-balance-pass1.md`。

战斗内 buff：`Δattack = ΔjobPrimary + Δstrength`（再加 skill.dsh 直接攻击变化）。
战斗丹攻击% 仍在伤害结算前叠乘。

## 修改点

- `src/server/mock_server_role.c`：`vm_net_mock_role_build_player_stats`、
  `vm_net_mock_battle_apply_active_stat_modifier`
- 模型说明：`2026-06-28-player-attribute-model.md`、
  `2026-07-31-battle-attack-job-primary-attr.md`

## 验证

- `make -j2`
- 幻剑：加敏装 → 普攻约 `+Δ敏`；加力量装 → 另 `+Δ力`（即使敏为主属性）
- 天机：加力量 → 约 `+2×Δ力` + 装备攻击全额
- 鬼道：加智 → `+Δ智`；加力 → `+Δ力`
