# 2026-10-03 高等级怪物 HP 额外 ×3 后台可配

## 变更

开战读属性时，「等级倍率 ≥6（Lv≥56）对 HP 再 ×3」改为管理后台开关，不再写死。

默认关闭（与当日取消硬编码 ×3 的行为一致）。勾选后立即热加载，仅 HP 多乘 3；MP/攻击/防御仍只乘档位倍率。首领 HP×5、全服血量百分比仍在其后叠加。

## 存储

`server_monster_global_config.hp_extra_x3_when_mult_ge6`：`TINYINT`，0=关，1=开。

- 运行时 `ALTER`：`vm_net_mock_monster_global_ensure_column`
- 手工迁移：`server/mysql/migrate_add_monster_hp_extra_x3.sql`

## 修改点

- `vm_net_mock_monster_stats_apply_combat_attr_multiplier()`：开关开启且档位倍率 ≥6 时 HP ×3
- 后台 `?tab=monsters` 全服表单复选框 → `save-monster-global-config`

## 验证

- [ ] `make -j2`；服务重启；`healthz` 200
- [ ] 未勾选：Lv≥56 开战 HP 仅为档位倍率（及首领×5 / 全服%）
- [ ] 勾选保存后再开战：同怪 HP 约为未勾选的 3 倍
- [ ] 未勾选保存不会误开（checkbox 未提交视为 0）
