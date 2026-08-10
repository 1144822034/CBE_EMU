# 2026-08-05 装备强化护甲/物防加成减半

## 触发

穿戴强化装备后物防偏高，需下调强化带来的护甲贡献；物攻强化保持原契约。

## 契约

- 物攻 `M(L)` 不变：`scaled = base + base*(10L+35⌊L/4⌋)/100`（+16 → 4×）。
- 护甲基值不变；**强化增量**为满量 `M(L)-base` 的一半：
  `armor = base + (M(L)-base)/2`（+16 → 2.5× base）。
- 护甲里程碑品质表同步减半：`87/175/262/350`（原 `175/350/525/700`）。
- 战斗物防面板值仍为 `defense = 4 + level + endurance/2 + equipment.armor`（全额）。
- 普攻减伤自 2026-08-10：`bare + equipment.armor/5`，软曲线 K=2000
  （`2026-08-10-na-defense-scale-and-soft-curve-k.md`）。

## 修改点

- `mock_server_catalog.c`：`armor_bonus_scale` / `armor_enhance_delta`；`kArmorByQuality`。
- `mock_server_role.c`：`collect_equipment_bonus` 护甲走半量增量。
- `mock_server_scene_sync.c`：商店/出售详情护甲 `(+Δ)` 与结算一致。

## 验证

- [ ] 同件防具未强化 vs +16：护甲贡献约 2.5× 基值（非 4×）；物攻仍约 4×。
- [ ] 强化附加护甲词条与人物物防一致；品质档护甲里程碑约为原值一半。
- [ ] `make -j2`
