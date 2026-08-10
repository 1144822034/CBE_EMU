# 普攻：战斗护甲 /5 + 软曲线 K=2000

日期：2026-08-10

## 触发

面板护甲已上万、物攻不足其一半时，旧契约（战斗用全额护甲 + `K=100`）把普攻压到攻击约 1%，互砍/怪普攻手感崩坏。

## 契约

| 用途 | 公式 |
| --- | --- |
| 面板 / Pixi 1/1/21 / actorinfo 护甲字 | `defense = bare + equipment.armor`（全额，不变） |
| 普攻减伤用的有效防 | `mitigation = bare + equipment.armor / 5` |
| 软曲线 | `damage = (attack * 2000 + mitigation/2) / (2000 + mitigation)`，至少 1 |

- `bare = 4 + level + endurance/2`（及战斗 buff 叠在 `stats->defense` 上、扣掉 `equipment.armor` 后的剩余）
- 怪物 `defense_value` **不做** `/5`，只吃新的 `K=2000`
- 进攻技能仍走法抗 `%`，不走护甲曲线
- 切磋普攻与单体技能相对 NA 的参考伤害同一套 mitigation

约值：面板护甲 10000、物攻 4000 → 战斗 def≈2100 → 普攻约 **1900+**（约攻的一半）；旧路径约 40。

## 修改点

- `mock_server_role.c`：`vm_net_mock_damage_after_defense`（K=2000）、`vm_net_mock_battle_mitigation_defense`；怪→玩家 NA、`role_defense_default`
- `mock_server_battle.c`：切磋 NA / 单体技能 NA 参考

## 验证

- [ ] `make -j2`
- [ ] 面板护甲仍为全额；穿脱防具后数字同向变化
- [ ] 同装互砍普攻约数百～两千，不再两位数
- [ ] 怪技能仍只看法抗；怪普攻随玩家装甲变化（/5 后）
- [ ] 低防怪被玩家普攻会明显高于旧 K=100（预期；必要时另调怪防表）
