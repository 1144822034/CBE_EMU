# 2026-08-03 物理技能改走抗性减伤

## 变更

进攻技能（天机/幻剑物理技 + 鬼道法术）统一用抗性百分比减伤：

```text
damage = raw * (100 - min(resist, 70)) / 100
```

普攻仍用防御软减伤 + pierce。

| 路径 | 减伤 |
| --- | --- |
| 普攻 | `damage_after_defense`（怪/人 `defense`） |
| 进攻技能 | `damage_after_resist`（怪次级抗性 / 人装备抗性） |

- 物技与法术均忽略闪躲（必中）；仅普攻仍检定闪躲（2026-09-27）。
- 怪抗性默认仍为 0（`CBE_BATTLE_MONSTER_RESIST` / `CBE_BATTLE_SKILL_ENEMY_RESIST`）。
- 切磋同步：技能 → 抗性；普攻 → 防御。

## 修改点

- `mock_server_role.c`：`vm_net_mock_battle_player_skill_damage_to_enemy`
- `mock_server_battle.c`：切磋 `duel_apply_operate` 伤害分支
- `2026-07-29-battle-hit-crit-resist.md`

## 验证

- `make server -j2`
- 物技对怪：默认抗性 0 → 接近 raw（无护甲软曲线/pierce）
- 物技切磋：对方抗性装备应明显压伤；对方高防低抗时物技应比普攻吃抗更多
