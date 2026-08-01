# 强化附加里程碑按装备品质定值（2026-07-29）

## 变更

`+4/+8/+12/+16` 四档词条的 `value` 不再用 `max(1, base*10/100)`，
改为按 `equip.dsh` 列「品质」计算（`equip->quality`）。

| 类型 | 公式 |
| --- | --- |
| 暴击 / 闪躲 / 命中 | `品质 + 1` |
| 气血% / 法力%（flag=1） | `(品质 + 1) * 5` |
| 力量 / 敏捷 / 智慧 | `(品质 + 1) * 120`（三围词条固定在 +16） |
| 气血 / 法力（值） | 品质 1→150，2→300，3→450，4→600 |
| 物攻 | 1→23，2→50，3→123，4→296 |
| 护甲 | 1→175，2→350，3→525，4→700 |

白装 `品质=0`：加减类仍用 `0+1`；定表类回退到档 **1**。
`品质>4`：定表钳到档 4。

里程碑模板（哪一档出哪一 type）不变；仅 `value` 来源变了。
后面追加的 `M(L)-base` 护甲/物攻/气血/法力行不受本次影响。

## 人物属性（2026-07-29 续）

此前 `collect_equipment_bonus` 只做 `M(L)` 缩放，**未加**已解锁里程碑，
导致强化附加显示暴击+3 而人物暴击几乎不变。

现对每件穿戴装在 `M(L)` 之后调用
`vm_net_mock_equipment_bonus_add_unlocked_milestones`：凡 `unlock<=L` 的
品质定值词条写入 bonus（进 `actorinfo` / 战斗）。气血%/法力% 为
`装备基值 × 百分数 / 100`。

## 代码

`vm_net_mock_equip_enhance_attr_value_for_type(quality, type, flag)`
`vm_net_mock_equipment_bonus_add_unlocked_milestones`（`mock_server_catalog.c`）
→ `vm_net_mock_role_collect_equipment_bonus`（`mock_server_role.c`）。

## 验证

- [ ] 同部位白装 vs 高品质装：强化到 +4 后里程碑数值随品质变大。
- [ ] 上衣 +8 气血%：白装约 5%，品质 1 约 10%（`(Q+1)*5`）。
- [ ] 武器品质 2、+4：强化附加暴击+3，人物暴击等于装备暴击合计（无等级/三围底、无 `/2`）。
- [ ] 强化后详情/背包同步仍正常；`make server -j2`。
