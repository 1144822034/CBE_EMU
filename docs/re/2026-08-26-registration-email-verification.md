# 注册开关、SMTP 与邮箱验证注册

## 目的

后台新增“注册设置”页，用于管理游戏内自动 guest 账号分配及网页注册使用的
SMTP 邮件服务器。网页注册不再直接写入 `accounts`：用户必须先接收并提交邮箱
验证码，成功后邮箱会唯一绑定到新账号。

## 配置与迁移

先在目标 `jh_online` 数据库执行：

```sql
SOURCE server/mysql/migrate_add_registration_email_settings.sql;
```

服务在首次读取注册配置时也会以 `CREATE TABLE IF NOT EXISTS` 补齐相关表，
以保证升级期间不会意外回退到旧的内存状态：

- `server_registration_config`：`allow_game_auto_account` 开关，默认开启。
- `server_smtp_config`：SMTP 主机、端口、认证凭据、发件邮箱和启用状态。
- `server_registration_email_templates`：注册邮件的主题与正文模板。
- `account_email_bindings`：账号到已验证邮箱的一对一绑定。
- `web_registration_email_verifications`：短期验证码摘要、失败次数和到期时间。
- `web_registration_image_captchas`：发送邮件验证码前的图片验证码挑战；只保存答案摘要。

后台不会回显 SMTP 密码，也不会把它写入操作日志；保存时密码字段留空会保留
已有值，勾选“清除已保存的 SMTP 密码”才会删除它。

“注册验证码邮件”区可直接编辑主题和正文。正文必须保留 `{{code}}` 占位符；服务仅在
投递时将它替换成当次生成的六码，数据库与日志都不保存明文验证码。主题禁止换行，正文
会在发送前统一为 SMTP 换行并对行首的 `.` 转义，避免自定义文本意外结束 `DATA` 内容。

## 网页注册契约

1. 注册表单只有一个邮箱输入框，位于密码下方、六码邮件验证码输入框上方。
2. 点击“发送邮箱验证码”会弹出图片验证码。浏览器以 `POST /user/register/captcha/new`
   为该邮箱取得一张 SVG 图片与一次性挑战令牌；答案只保存在浏览器显示的图片中。
3. 浏览器将令牌和答案随 `POST /user/register/code` 提交。服务会核对该挑战是否属于同一
   规范化邮箱、未过期且未超过失败次数；正确答案会立即消耗该挑战，错误答案绝不会调用 SMTP。
4. 图片验证码有效期 5 分钟，最多 5 次尝试，令牌由系统安全随机源生成；邮件验证码仍受每邮箱
   60 秒重发间隔限制。
5. 服务检查 SMTP 配置、每邮箱 60 秒的重发间隔，并以系统安全随机源产生六码。
6. 服务使用 SMTP `EHLO`、可选 `AUTH PLAIN`、`MAIL FROM`、`RCPT TO`、`DATA`
   发送管理员配置的邮件主题和正文；正文中的 `{{code}}` 才会在此时替换为该次六码。
   仅在投递成功后持久化验证码的 MD5 摘要。
7. `POST /user/register` 必须同时带 `account`、`password`、`email`、
   `verification_code`。验证码有效期 10 分钟、最多验证 5 次。
8. 验证成功时，一个 MySQL 事务同时插入 `accounts`、`account_wallets`、
   `account_email_bindings`，删除验证码行并提交。任一失败会回滚，因此不会留下
   无邮箱绑定的网页注册账号。

邮箱在 `account_email_bindings.email` 上唯一。已有账号和后台手工创建的账号不被
迁移强制补绑；这一功能只约束新增的网页注册账号。

## 游戏内自动分配

游戏客户端发出已取证的无账号 WT 登录请求时，传输层先读取
`server_registration_config.allow_game_auto_account`。关闭时仅拒绝没有现有会话的
新分配请求；已为该客户端会话创建的 guest 账号仍能重新取得自己的凭据，不会被
错误地当作一条新建账号请求。

## 后台保存审计修复（2026-08-26）

**触发条件：** 在“注册设置”页面保存自动分配或 SMTP 配置。

**首个偏离与根因：** 配置已经写入后，操作日志的插入路径把
`target_account_id` 传成空字符串；而日志记录函数的既有契约要求此字段非空。它会在
执行 SQL 前拒绝请求，因而页面会错误提示“请检查数据库”。

**修复：** 所有后台全局配置操作统一使用 `admin-config` 作为非账号类操作的审计目标。
注册设置保存现在复用该标识；回归检查会验证空表单目标被归一化为同一标识。SMTP 密码
仍不会进入日志。

## 图片验证码 CSP 兼容修复（2026-08-26）

**触发条件：** 账号中心启用严格 CSP（`script-src 'self'`）后，点击发送邮箱验证码。

**首个偏离与根因：** 注册页最初把弹窗逻辑写在页面内嵌脚本和 `onclick` 属性中；该策略
正确拒绝两者，导致按钮没有行为。图片最初采用 `data:` 地址，同样不属于 `img-src 'self'`。

**修复：** 交互逻辑迁至本站 `/user/register.js`，由事件监听器绑定带数据属性的按钮；服务端
返回受控 SVG 字符串，脚本经 SVG XML 解析后插入验证码容器。没有加入 `unsafe-inline`、
`unsafe-hashes` 或额外脚本来源，原有 CSP 继续生效。

## 验证码立即过期修复（2026-08-26）

**触发条件：** MySQL 会话时区不是 UTC（例如服务器使用中国标准时间）时，正确输入刚获取的
图片验证码仍显示“无效或已过期”。

**首个偏离与根因：** 验证码表的字段类型是 `TIMESTAMP`，创建记录却传入 `UTC_TIMESTAMP()`。
MySQL 会把这段 UTC 墙上时间当作当前会话时区的本地时间写入；随后 `UNIX_TIMESTAMP` 与服务端
当前 Unix 时间比较，记录会偏移一个时区并被立即视为过期。邮件验证码采用同一写入方式，也有
相同隐患。

**修复：** 图片和邮件验证码都改用 `CURRENT_TIMESTAMP()` 写入与清理。这让 `TIMESTAMP` 的写入、
`UNIX_TIMESTAMP` 读取及服务端当前时间处于同一会话时区基准；升级后的新验证码会正常有效，
此前已生成的验证码需要重新获取。

## SMTP 安全边界

当前无 OpenSSL/SChannel 依赖的服务构建实现的是普通 SMTP relay 协议，不协商
STARTTLS 或 SMTPS。生产环境必须把 SMTP 主机配置为本机或受信任内网中已经完成
TLS 转发的 relay（例如 MTA、Stunnel 或云服务商的本地代理），不要把 SMTP 认证
凭据指向公网的明文端口。后台页面会显示同一限制。

SMTP 投递失败会在服务标准输出中记录 `smtp_delivery_failed`，但不会记录收件人、
认证账号、密码或验证码。`stage` 依次可能为 `resolve_ipv4`、`connect`、`greeting`、
`ehlo`、`auth_plain`、`mail_from`、`rcpt_to`、`data` 与 `data_result`；`expected` 和
`received` 记录 SMTP 状态码。部署在阿里云 ECS 时，直连 `smtpdm.aliyun.com:25` 通常
会在 `connect` 阶段失败，因为该端口默认被禁用；应使用 TLS 终结在本机/内网的 relay，
或先按云网络策略开通合适的出站路径。

## 验证

```text
make -j2
make registration-email-contract-regression
obj/server/registration-email-contract-regression.exe
```

该回归覆盖邮箱规范化、六码验证码格式、图片验证码令牌/字符集、SMTP `AUTH PLAIN`
Base64 编码、邮件模板占位符替换、SMTP 正文行首转义、最小 relay 配置和后台导航契约。
实际投递需使用隔离的 SMTP 测试 relay 与独立数据库，并核对图片验证码消耗、邮件内容、
验证码消耗、`account_email_bindings` 和账户创建事务。
