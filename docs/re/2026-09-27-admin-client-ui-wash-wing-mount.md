# 客户端 UI 开关（洗练按钮 / 翅 / 骑）

日期: 2026-09-27

## 需求

管理后台可分别开关：

1. 装备「洗练」按钮是否显示
2. 地图角状态栏「翅」是否显示
3. 地图角状态栏「骑」是否显示

## 实现

### 配置

- 表 `server_client_ui_config`（`id=1`）：
  - `wash_button_enabled` 默认 1
  - `wing_status_enabled` 默认 1
  - `mount_status_enabled` 默认 1
- 运行时：`src/server/mock_server_client_ui_config.inc.c`（`CREATE IF NOT EXISTS` + `INSERT IGNORE`）
- 迁移脚本：`server/mysql/migrate_add_client_ui_config.sql`
- 后台 Tab「客户端UI」：`src/web_admin_client_ui.inc.c`，动作 `save-client-ui-config`

### 下发

人物面板 `1/1/21` 增加：

| 字段 | 含义 |
|------|------|
| `washButtonEnabled` | 1=显示洗练 |
| `wingStatusEnabled` | 1=显示翅 |
| `mountStatusEnabled` | 1=显示骑 |
| `multiOpenLimit` | 本机多开窗口上限（1–8；见 `2026-09-28-client-multi-open-windows.md`） |
| `hangupQuotaWithoutInsight` | 无战斗心得时是否限制挂机 64 场（1=限制，0=不限；见 `2026-09-29-hangup-battles-quota-insight.md`） |

旧包缺字段时客户端默认按开（与库默认一致）。

### 关闭时契约

| 路径 | 关闭后 |
|------|--------|
| 背包/强化「洗练」 | 不出现按钮 |
| 29/7–9、29/14 | `result=7`，提示「洗练未开放」 |
| 29/13 星空石 | `result=8`，提示「洗练未开放」 |
| 地图角「翅」「骑」 | 不占位、不可点；已装备外观与解锁不受影响 |

### 客户端

- `CharacterStats` 解析上述三字段
- `SystemPanel` 按 `washButtonEnabled` 决定是否推「洗练」
- `SceneView.setChiVisible` / `setQiVisible` 跟随面板刷新

## 验证

1. 后台「客户端UI」→ 洗练隐藏 → 保存；拉面板后背包/强化无「洗练」；强发 29/7 得 result=7
2. 翅/骑隐藏 → 状态栏无对应格；开回后重拉面板出现
3. 三项默认显示时行为与改前一致
