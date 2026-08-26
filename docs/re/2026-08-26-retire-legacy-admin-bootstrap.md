# 移除旧版后台默认账号兼容逻辑（2026-08-26）

状态：已实现。

## 根因

`vm_mock_admin_user_schema_ensure()` 在每次后台凭据校验前都会执行
`INSERT IGNORE ... SELECT 'admin' FROM server_admin_config`。因此一旦现有 `admin` 被删除，
下一次后台登录就会从旧配置表补建它；重启后的首个登录尝试容易让该现象看起来像启动时创建。

## 修改

- 运行时只保证 `server_admin_users` 表存在，不再读取或迁入 `server_admin_config`。
- 新库 schema 和 `migrate_admin_users.sql` 都不再创建旧配置或默认 `admin`。
- `migrate_add_web_accounts.sql` 保留为无写入的退役提示，避免旧部署脚本再次创建默认凭据。
- 后台回归夹具改为显式创建其隔离的 `npc-stock-admin` 操作员账号。

## 运维边界

现有的 `server_admin_config` 表和现有 `server_admin_users.admin` 不会由此次代码变更自动删除。
确认至少有一个可登录的替代操作员后，可由可信数据库管理终端自行删除旧表或旧账号。新部署
必须先向 `server_admin_users` 显式插入首个操作员，再进行后台登录。
