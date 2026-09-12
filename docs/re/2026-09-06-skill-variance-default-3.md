# 2026-09-06 技能伤害浮动默认 8%→3%

## 契约

`skill_variance_percent` 默认由 ±8 改为 ±3（PvE + 决斗，暴击前）。普攻 `na_variance_percent` 仍为 ±5。

## 落库

启动 `combat_formula` ensure：若 `id=1` 仍为旧默认 `8`，写回 `3`；列 DEFAULT 同步改为 3。后台若已改成其它值则不动。

## 修改点

- `mock_server_combat_formula_config.inc.c` / `migrate_add_combat_formula_config.sql`
- `mock_server_role.c` / `recovered_helpers.inc` / `web_admin_combat.inc.c` 回退默认

## 验证

- 重启服务后管理后台「技能浮动」应为 3（若此前未手工改过）
- 同技能连续命中未暴击：飘字约在 ±3% 内
