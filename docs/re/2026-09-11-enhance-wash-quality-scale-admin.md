# 装备洗练规则后台配置（质量分档 + 首获概率）

日期: 2026-09-11

## 需求

1. 洗练属性池上限按装备品质分档可开关（默认开）。
2. 首次获得装备时从洗练池随机四档的概率可调（原硬编码 20%，默认仍 20）。

## 实现

- 表 `server_enhance_wash_settings`（`id=1`）：
  - `quality_scale_enabled` 默认 1
  - `first_seed_percent` 默认 20（0–100）
- 运行时：
  - `vm_net_mock_enhance_wash_pool_scaled_max`：关则返回完整 `valueMax`
  - `vm_net_mock_enhance_wash_seed_initial`：用 `first_seed_percent`；0=永不、100=必定
- 影响路径：首获 seed、玩家洗练 roll、29/7 pool（质量分档）；仅首获入包（首获概率）。
- 后台：强化配置页顶部「洗练规则」，动作 `enhance-wash-settings-save`。
- 存量库：`server/mysql/migrate_add_enhance_wash_quality_scale.sql`；进程 `CREATE IF NOT EXISTS` + 缺列时 `ALTER`。

## 验证

1. 质量分档关 → 低品质装 pool/roll 上限 = `value_max`
2. 质量分档开 → Q0≈1/5、Q4 满值
3. 首获概率改 0 → 新获得装备不写随机 wash
4. 改 100 → 新获得几乎必定四档随机（wash_count=0）；Pixi 装备名不再标 `[洗]`
5. 保存后立即生效（1/1/21 `washFirstSeedPercent`）
