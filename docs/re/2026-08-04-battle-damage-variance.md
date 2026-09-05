# 2026-08-04 战斗伤害 ±5% 波动

## 触发

pierce 软上限后普攻数值稳定，同一目标每次飘字完全相同，缺少手感波动。

## 契约

伤害波动（暴击前）：

- 普攻 / 怪反击：±5%
- 进攻技能：±8%

`CBE_BATTLE_DISABLE_COMBAT_ROLLS=1` 时关闭。

## 修改点

- `mock_server_role.c`：`vm_net_mock_battle_apply_damage_variance`（后曾仅决斗路径调用；2026-09-04 起 PvE 再次接入，见 `2026-09-04-pve-damage-variance.md`）
- `mock_server_battle.c`：切磋伤害路径

## 验证

- `make -j2`
- 连续普攻同怪：飘字应在约 ±5% 内变化（未暴击时）
