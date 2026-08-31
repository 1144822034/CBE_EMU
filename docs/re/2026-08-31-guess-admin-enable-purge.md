# 猜猜看后台开关与 3 天记录清理

日期: 2026-08-31

## 需求

娱乐「猜猜看」接入后台管理：支持开启/关闭；自动清除玩家 3 天前的押注记录。

## 实现

### 配置

- 表 `server_guess_config`（`id=1`, `enabled`），默认开启。
- 后台 Tab「猜猜看」：`src/web_admin_guess.inc.c`，动作 `save-guess-config`。
- 运行时：`vm_mock_guess_enabled()`；WT45 status 增加 `enabled`；关闭时 `bettingOpen=0` 且 bet 返回 `disabled`。
- 关闭后仍可 history / claim（领取未领奖励）。

### 客户端入口

- 打开「娱乐」时先拉 guess status；`enabled=0` 时列表不显示「猜猜看」。
- 若仍打开面板且未开放，直接关闭面板。

### 清理

- `vm_mock_guess_tick` 内每小时执行一次：
  - `DELETE server_guess_bets WHERE created_at < now-3d`（含逾期未领）
  - 删除无下注且已开奖、`period_end` 早于保留窗的 `server_guess_rounds`

### SQL

- `server/mysql/migrate_add_guess_admin.sql`（存量库）
- `migrate_add_guess.sql` / `schema.sql` 已同步

## 验证

1. 后台关 → 娱乐列表无「猜猜看」；押注 API 仍返回 `disabled`
2. 后台开 → 列表出现并可押注
3. 造 `created_at` 超过 3 天的 bet，等 purge 或重启后下一小时 tick，记录消失
