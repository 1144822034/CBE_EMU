# 面板：力量→气血 / 智慧→法力可配置系数

## 范围

`vm_net_mock_role_build_player_stats` 裸气血/法力池在原有等级/耐力/成长智慧×3 公式上，额外：

```text
bareHp += strength * panel_strength_hp_coeff_x100 / 100
bareMp += wisdom   * panel_wisdom_mp_coeff_x100   / 100
```

默认 `x100=50`（即加成 0.5）。`strength` / `wisdom` 为面板合计（成长 + 装备）。
成长智慧×3 仍只用 `baseWisdom`，避免装备智慧再乘 3。

## 入口

- 管理页：`?tab=combat` →「面板属性」
- 列：`panel_strength_hp_coeff_x100` / `panel_wisdom_mp_coeff_x100`
- 迁移：`server/mysql/migrate_add_panel_str_hp_wis_mp_coeff.sql`（运行时也会 `ALTER`）

## 代码

- `mock_server_combat_formula_config.inc.c` — 缓存 / 读写 / 默认 50
- `mock_server_role.c` — bareHp/bareMp
- `web_admin_combat.inc.c` — 表单字段

## 验证

1. 启动后打开战斗公式页，两字段默认 50。
2. 改力量系数保存；穿/脱力量装后气血随面板力量×系数/100 变化。
3. 智慧系数同理叠加在 `baseWisdom×3` 之外，装备智慧只走 ×系数/100。
