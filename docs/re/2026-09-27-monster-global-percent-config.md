# 全服怪物百分比配置

## 需求

后台「怪物管理」增加全服运行时倍率：血量、攻击、防御、掉落概率、经验、金钱。
100% 不变，90% 降低，110% 提高。各怪编辑页仍只改基数。

## 存储

单例表 `server_monster_global_config`（`id=1`）：

| 列 | 默认 | 范围（sanitize） |
|----|------|------------------|
| hp_percent | 100 | 1–1000 |
| attack_percent | 100 | 1–1000 |
| defense_percent | 100 | 0–1000 |
| drop_rate_percent | 100 | 0–1000 |
| reward_exp_percent | 100 | 0–1000 |
| reward_money_percent | 100 | 0–1000 |

迁移：`server/mysql/migrate_add_monster_global_config.sql`；服务首次读取也会建表。

## 运行时应用点

1. **HP / 攻击 / 防御 / 经验 / 铜钱**  
   `vm_net_mock_monster_stats_apply_combat_attr_multiplier()`：在等级倍率与首领 HP×5 之后再乘全服百分比。  
   管理列表/保存仍走 `_raw`，不会把倍率写回 MySQL。

2. **掉落概率**  
   结算投掷前 `vm_net_mock_monster_global_effective_drop_rate()`：  
   `min(1000, configured_rate_x10 * drop_rate_percent / 100)`  
   （`configured_rate_x10`：1=0.1%，1000=100%）。  
   独立行 / 抽奖行的选择仍按配置表原始概率（含 100% 独立），只缩放最终投掷。

3. **角色侧加成**  
   经验卡、战斗心得、姻缘、打金心得等仍叠在怪物基础奖励（已含全服%）之后，可审计。

## 后台

`?tab=monsters` 顶部「全服怪物百分比」表单 → `save-monster-global-config` → 热加载缓存。

## 验证建议

- 全 100：开战 HP/攻击与改前一致。  
- 血量 90 / 攻击 110：场景开战 HP 与伤害相对基数×等级倍率后再乘。  
- 掉落 50：日志 `effective_rate` 约为配置概率一半。  
- 经验 200：基础结算经验翻倍后再叠卡/心得。  
- 改单怪基数后保存：库内仍为基数，不受全服%影响。
