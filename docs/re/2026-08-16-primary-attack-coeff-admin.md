# 分职业主属性物攻加成系数可配置

日期：2026-08-16

## 契约

面板与战斗共用：

```text
attack = jobPrimary * coeffPercent / 100 + equipment.attack  (cap 65535)
```

| 职业 | jobPrimary | 配置项 | 默认 |
|------|------------|--------|------|
| 天机 | strength | primary_attack_coeff_tianji_percent | 100 |
| 幻剑 | agility | primary_attack_coeff_huanjian_percent | 100 |
| 鬼道 | wisdom | primary_attack_coeff_guidao_percent | 100 |

战斗 buff 对主属性的 Δ 同步按同一系数折算到 `attack`。

## 入口

- 管理页：`?tab=combat` →「主属性物攻加成系数」
- 表列：`server_combat_formula_config.primary_attack_coeff_*`
- 迁移：`server/mysql/migrate_add_primary_attack_coeff.sql`（启动时也会补列）

## 修改点

- `mock_server_combat_formula_config.inc.c`：字段 / load / save / ensure column
- `mock_server_role.c`：`build_player_stats`、`battle_apply_active_stat_modifier`
- `web_admin_combat.inc.c`：表单项

## 验证

1. `make -j2`
2. 打开战斗公式页，三职业系数默认 100；改幻剑为 50 保存后，同敏角色面板/普攻物攻约减半主属性贡献。
3. 战斗内加敏 buff：幻剑物攻变化约 `Δ敏 × 50%`。
