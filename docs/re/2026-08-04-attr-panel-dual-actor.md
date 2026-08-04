# 2026-08-04 属性面板读错 actor（5CA4 vs 5CAC）

## 现象

穿脱装备后只有气血/法力变化；力量/敏捷/智慧/物攻/护甲/闪躲/命中/暴击/抗性
面板数值不变。

## 运行时证据（session a1d304）

| 假设 | 结果 | 证据 |
| --- | --- | --- |
| P1 双 actor | **确认** | `same=0`，`a5CA4≠b5CAC` |
| P2 FA 写入 5CAC | **确认** | `after_fa_apply atk130=576→2600`，尾声 `b_atk/b_armor` 正确 |
| P3 vitals 下发 | **确认投递、未解析** | `map_vitals_delivered` 有；无 `parse_actorinfo a2≠0` |
| 面板读 5CA4 | **确认** | realign 一直画 `a_*=裸装` |

穿齐后示例：`b_atk=2600 b_armor=17911 b_dodge=4 b_hit=106 b_crit=5`，
同时 `a_atk=0 a_armor=183`。

三围：名表 type 1/2/3 的 wear-apply jump 写到魅/力/敏，智慧无 jump；
`b_agi` 被智里程碑抬高，`b_str` 仍裸。

## 修复

扩展既有 host 展示层 `vm_host_realign_attr_panel_value_rows`：

- 物攻/护甲/闪躲/命中/暴击/抗性：读 **R9+0x5CAC**（wear-apply 权威）
- 三围：在 5CA4 仍为 actorinfo 基线时，用 **有符号** jump 错位增量还原  
  （力←魅差，敏←力半字差，智←敏半字差）；登录 actorinfo 必须种裸三围
- 魅力行仍用 5CA4，避免把力量误写进魅力显示

不写 CBE 半字、不 patch 跳转。换装后不再追加/武装 map `1/1/14`（会污染商城，
见 `2026-08-04-login-equip-panel-shop-crash.md`）。

## 验证

1. 裸装记面板 → 穿武器+防具+帽子：物攻/护甲/闪躲/命中/暴击/力/智应涨。
2. 卸下后回到穿前。
3. 日志 `P4_fix attr_rows_realigned`：`atk/armor` 非裸装值。
4. `make -j2`
