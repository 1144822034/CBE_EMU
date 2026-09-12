# 每日注册上限后台可调

日期: 2026-09-11

## 需求

全服每日自助注册名额（原硬编码 3）放到后台「账号管理」可调。

## 实现

- 表 `server_registration_config`（`id=1`, `daily_limit`，默认 3）。
- 运行时：`vm_mock_service_register_daily_limit()`；`try_reserve` 使用该值；`0` 表示禁止自助注册。
- 后台账号管理页顶部表单，动作 `save-registration-config`。
- 后台「创建账号」仍走 `account_create_record`，不占/不受每日限额。
- 存量库：`server/mysql/migrate_add_registration_config.sql`（进程也会 `CREATE IF NOT EXISTS`）。

## 验证

1. 后台改成 1 → 自助注册满 1 后拒绝；后台手动创建仍成功  
2. 改成 0 → 自助注册立即拒绝  
3. 改回 3 → 按新上限计数（当日已用计数不清零）
