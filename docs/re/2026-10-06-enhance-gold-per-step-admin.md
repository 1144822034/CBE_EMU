# 装备强化消耗金币后台配置（2026-10-06）

## 需求

装备强化扣金币原先写死为 `(当前强化等级+1)×5` 金。管理后台「强化配置」可改每档金币数量，保存后立即生效。

## 契约

- 路径：WT 29/2 预览、29/3 确认（`vm_net_mock_equipment_enhance_money_cost`）。
- 公式：`金币 = (当前强化等级+1) × enhance_gold_per_step`；铜币 = 金币 × 10000。
- 默认 `enhance_gold_per_step=5`（与原硬编码一致）：+0→+1 扣 5 金，+15→+16 扣 80 金。
- `0` = 不扣金币。范围 0–10000。
- 不影响洗练（29/9）与重铸（29/6）费用。

## 表

`server_enhance_wash_settings.enhance_gold_per_step`（`id=1`）。

存量库：`server/mysql/migrate_add_enhance_gold_per_step.sql`；进程 `CREATE IF NOT EXISTS` + 缺列 `ALTER`。

## 后台

`/?tab=enhance` 卡片「强化消耗金币」，动作 `enhance-gold-cost-save`。

## 验证

1. `make -j2`
2. 后台改每档为 1 → 强化预览/确认扣 `(L+1)` 金
3. 改为 0 → 不扣金币
4. 改回 5 → 与改前产品费用一致
5. 重启后配置仍在
