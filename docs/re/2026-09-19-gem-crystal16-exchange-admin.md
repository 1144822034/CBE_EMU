# 十六级玄晶换宝石后台开关

日期: 2026-09-19

## 需求

管理后台可开启/关闭「十六级玄晶（916）兑换随机 1 级宝石」；仅开启时允许 29/21。

## 实现

### 配置

- 表 `server_gem_config` 新增列 `crystal16_exchange_enabled`（TINYINT，默认 0）。
- 启动/保存时 `ensure_column` 自动补列；缺省关。
- 后台 Tab「宝石配置」：`src/web_admin_gem.inc.c`，动作 `save-gem-config` 增加下拉「允许十六级玄晶兑换宝石」。
- 保存：`vm_net_mock_gem_config_admin_save(..., enabled)` → 写库并强制重载内存。

### 关闭时契约

| 路径 | 关闭后 |
|------|--------|
| 29/21 兑换 | 拒：`result=7`，`message=十六级玄晶兑换未开放`，不扣晶 |
| 人物面板 `1/1/21` | 下发 `crystal16ExchangeEnabled=0` |
| 客户端背包 916 | 不显示「换宝石」 |

其它宝石操作（镶嵌/拆除/合成/属性兑换/碎片）不受影响；`crystal16_gem_count` 仍可配置，仅在开关为开时生效。

### 客户端

- `CharacterStats.crystal16ExchangeEnabled` 来自面板。
- `SystemPanel` 仅在开关为 1 时对 916 提供「换宝石」。
- `GameSession.gemFromCrystal16` 本地先拦一层，避免无按钮时仍发请求。

## 验证

1. 后台「宝石配置」→ 关 → 保存；重开背包/拉面板后 916 无「换宝石」；强发 29/21 得 result=7
2. 后台开 → 保存；拉面板后出现「换宝石」，可按配置数量兑换
3. 关闭时其它宝石合成/镶嵌仍可用
