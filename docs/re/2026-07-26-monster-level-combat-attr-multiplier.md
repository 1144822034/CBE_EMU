# 怪物战斗属性等级倍率

## 需求

按怪物等级对战斗属性做默认倍率增强，使中高位区怪明显强于公式基数。首领（`family=BOSS`）血量在等级倍率后再额外 ×5。

## 规则

对 `hp` / `mp` / `attack` / `defense` 按等级取倍率（含该档下限）；**不**乘 `exp` / `gold`。

| 等级 | 倍率 |
| --- | --- |
| < 20 | 1 |
| ≥ 20 | 3 |
| ≥ 30 | 4 |
| ≥ 40 | 5 |
| ≥ 56 | 6（HP 再 ×3，即等级倍率×3） |
| ≥ 58 | 7（同上） |
| ≥ 60 | 8（同上） |
| ≥ 65 | 9（同上） |
| ≥ 68 | 10（同上） |
| ≥ 70 | 15（同上） |

首领额外：`family == BOSS` 时，在上述 HP 结果上再 ×5。攻防蓝不受此额外倍率影响。判定优先用 MySQL/后台覆盖的 family，否则用目录 family。

## 实现

- `vm_net_mock_monster_level_combat_attr_multiplier`
- `vm_net_mock_monster_family_for_enemy`
- `vm_net_mock_monster_stats_apply_combat_attr_multiplier`
- `vm_net_mock_monster_stats_for_enemy_raw`：公式或 MySQL 覆盖的未乘倍率基数
- `vm_net_mock_monster_stats_for_enemy`：战斗 / 场景刷怪 HP 等读取时再乘倍率

管理后台列表与保存走 `_raw`，避免把已乘倍率的数值写回库后二次放大。

## 验证

- [x] `make -j2`；服务重启；`healthz` 200
- [ ] 普通怪：20 级开战 HP/攻防约为后台基数的 3 倍
- [ ] 首领：同级普通倍率后的 HP 再 ×5；攻防蓝仅等级倍率
- [ ] 后台把 family 改为首领后进战，血量含额外 ×5
- [ ] 后台编辑保存后再进战，倍率只应用一次
