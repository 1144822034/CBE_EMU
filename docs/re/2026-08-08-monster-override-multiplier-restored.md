# 怪物 MySQL 覆盖恢复等级战斗倍率

日期：2026-08-08

## 现象

配置库 / 后台里的怪物战斗属性进战后整体变矮，像没有按数据库配置加载。

## 证据

- `server_monsters` 与后台编辑一直存的是 **基数**（列表走 `_raw`）。
- `2026-07-26` 契约：开战对 hp/mp/atk/def 乘等级倍率，首领 HP 再 ×5。
- 同日曾把「有 override 则跳过倍率」当成绝对值，基数未放大即进战。
- 同等级未覆盖怪仍乘倍率 → 覆盖怪相对「变少了」。

## 根因

首次偏离在 `vm_net_mock_monster_stats_for_enemy`：覆盖行提前 return，未调用
`vm_net_mock_monster_stats_apply_combat_attr_multiplier`。不是 SELECT 丢字段。

## 修改

覆盖与公式路径统一：先取基数（MySQL 或公式），再乘等级/首领倍率。
后台提示改回「基数 + 倍率」，撤销绝对值文案。

## 验证

1. `make -j2`；重启 mock。
2. 后台保存 Lv≥40 怪基数 HP=H → 开战 `combat_hp` / `enemyhp` = `H×5`（首领再×5）。
3. 同怪改攻击/防御基数后，反击伤害与普攻减伤应随倍率后的攻防变化。
