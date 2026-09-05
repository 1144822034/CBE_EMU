# 2026-09-04 PvE 伤害浮动

## 触发

决斗路径已有普攻 ±5% / 技能 ±8% 浮动（暴击前）；PvE 打怪与怪反击此前无随机浮动，同目标飘字恒定。玩家反馈与决斗手感不一致。

## 契约

伤害浮动（暴击前，与决斗同序、同后台参数）：

- 普攻 / 怪普攻反击：`na_variance_percent`（默认 ±5%）
- 进攻技能 / 怪技能反击（含 Boss 技能 remitate）：`skill_variance_percent`（默认 ±8%）

`CBE_BATTLE_DISABLE_COMBAT_ROLLS=1` 时关闭命中/暴击随机，并关闭浮动。

## 修改点

- `mock_server_role.c`：浮动实现迁入；玩家普攻/技能、怪反击、怪技能 remitate 在暴击前调用
- `mock_server_recovered_helpers.inc`：删除重复浮动实现（决斗仍用 role 中函数）
- `web_admin_combat.inc.c` / 公式配置注释：文案改为 PvE+决斗共用

## 验证

- `make -j2`
- 连续普攻同怪：未暴击飘字应在约 ±5% 内变化
- 进攻技能同怪：未暴击飘字应在约 ±8% 内变化
