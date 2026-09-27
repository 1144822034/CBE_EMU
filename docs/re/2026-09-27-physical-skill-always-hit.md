# 2026-09-27 物理技能与法术一样必中

## 变更

进攻技能（天机/幻剑物技 + 鬼道法术）统一忽略闪躲，必中。仅普攻仍走命中/闪躲检定。

| 路径 | 闪躲 |
| --- | --- |
| 普攻（PvE / 切磋 / 比武） | 仍检定（比武受 `allow_dodge` 约束） |
| 进攻技能（operate>2） | 必中，不再写 `child_flag=3` |

## 修改点

- `mock_server_role.c`：`vm_net_mock_battle_skill_ignores_dodge` → 任意非空 skill 即忽略闪躲
- `mock_server_battle.c`：切磋/比武伤害分支用 `skillIgnoresDodge` 替代原 `!magical` 门闩
- 同步：`2026-07-29-battle-hit-crit-resist.md`、`2026-08-03-physical-skill-resist-mitigation.md`、`2026-08-10-pixi-arena-biwu.md`

## 验证

- `make server -j2`
- PvE：物技对怪不再飘「闪躲」；普攻仍可闪躲
- 切磋/比武：物技必中；`allow_dodge=1` 时普攻仍可闪躲
