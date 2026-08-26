# 后台登录五次失败来源 IP 封锁与风险目录（2026-08-26）

状态：已实现；部署已有数据库时执行
`server/mysql/migrate_admin_login_ip_blocks.sql`。

## 触发与首次偏离

后台账号表 `server_admin_users` 原本会在同一账号连续密码错误 5 次后锁定该账号；来源 IP
则只会计入游戏、玩家账号中心和后台共用的 `server_login_ip_blocks`，阈值为 15 次。因而后台
入口没有“连续错误 5 次即封锁来源 IP”的独立契约，也无法在风险页面查到触发账号。

## 契约与实现

- `server_admin_login_ip_blocks` 以 IPv4 为主键，保存最近一次提交的后台账号、连续失败数、
  封锁状态、封锁时间和更新时间。
- 仅 `/admin-418yz6/login` 的凭据失败会递增该表。第 5 次失败仍返回原本的失败页；从下一次
  访问起，该来源的后台登录请求不会再进入认证或返回页面。
- 有效后台凭据且后台会话创建成功后，会删除该来源未封锁的后台失败记录，重置连续计数。
  已封锁来源不能自行解锁。
- 该表独立于 `server_login_ip_blocks`，避免后台误输密码阻断同一 IP 的游戏或玩家账号中心。
  原有全站 15 次来源 IP 封锁和后台账号 5 次锁定都保持不变。
- 风险管理新增 `risk_kind=admin-accounts`（“后台账号风险”）目录，展示后台账号、来源 IP、
  失败次数、封锁时间、更新时间和状态。账号是第 5 次错误触发封锁时提交的账号；格式无效或
  缺失的值以 `(invalid)` 留痕。

## 数据与恢复

新建数据库由 `server/mysql/schema.sql` 创建该表；已部署库可执行迁移。风险目录目前是只读审计
入口。人工解除前必须先核对来源和账号，然后在可信管理终端删除明确的单个 IP 行：

```sql
DELETE FROM server_admin_login_ip_blocks
WHERE ip_address = '203.0.113.7';
```

这不会解除同一后台账号自身的锁定；如该账号也因五次错误被锁定，按既有
`server_admin_users` 解锁流程处理。

## 验证

`scripts/risk-admin-pagination-regression.c` 覆盖后台账号风险目录链接、SQL 查询、页头和空表
渲染；`scripts/admin-request-length-regression.c` 覆盖后台 IP 阈值常量。完整构建仍执行
`make -j2`。
