# 2026-10-03 取消高等级怪物 HP 额外 ×3

## 变更

开战读属性时，等级倍率 ≥6（Lv≥56）对 HP 额外 ×3 **默认关闭**，改为管理后台可配。

见 `docs/re/2026-10-03-monster-hp-extra-x3-admin.md`。未勾选时 HP 只乘档位倍率本身，与 MP/攻击/防御一致。

首领 HP ×5、全服血量百分比不变。

## 修改点

- `vm_net_mock_monster_stats_apply_combat_attr_multiplier()`：HP 与其它战斗属性走同一 `multiplier`。
- `docs/re/2026-07-26-monster-level-combat-attr-multiplier.md`：表格去掉「HP 再 ×3」。

## 影响

Lv≥56 普通怪实战 HP 约为改前的 1/3；同级首领同样少这一层（仍有 ×5）。等级档位 ×6/×7/×8/×9/×10/×15 仍在。
