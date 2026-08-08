# W 币账号钱包迁移（2026-08-08）

## 触发条件与原有偏差

一个账号创建多个角色后，商城 `1/14/4.coolmoney`、`1/14/3` 购买、后台
“加 W 币”和充值回调都直接读写 `account_roles.wcoin`。该列的键是
`(account_id, role_id)`，所以同账号角色天然拥有不同余额；这与 W 币应归账号
而非角色的产品契约冲突。

客户端没有账号钱包字段：`mmShop` 仍只从当前角色形状的
`1/14/4.coolmoney` 读取余额。因此首个错误层在服务端关系模型与写入事务，不能
通过改 CBE 内存或另造客户端字段修复。

## 新的持久化契约

`account_wallets(account_id PRIMARY KEY, wcoin)` 是 W 币唯一权威来源：

- `account_roles.wcoin` 迁移完成后恒为 `0`，不再承载余额；
- 角色加载时把同一账号钱包余额填入各角色的运行时 `role->wcoin` 缓存，仅用于
  既有客户端角色/商城回包；
- 后台赠送和支付回调直接更新 `account_wallets`；充值订单继续保留 `role_id`，仅作
  下单记录，未支付订单的额度预留改为按账号汇总；
- 商城购买在已有的角色背包事务中 `SELECT ... FOR UPDATE` 锁定账号钱包，并把
  背包/扩容/直购效果与扣款一起提交。角色状态保存失败时钱包扣款同样回滚。

## 数据升级

启动服务、完成历史角色 payload 迁移之后，执行一次
`account-wcoin-wallet-v1`：

1. 用 InnoDB 事务锁定 `server_data_migrations` 中的标记；
2. 为全部账号补齐零余额钱包行；
3. 预检每个账号 `SUM(account_roles.wcoin)` 不超过客户端 `u32` 余额上限；
4. 将每账号的旧角色余额汇总写入 `account_wallets`；
5. 清零全部 `account_roles.wcoin`；
6. 写入升级标记并提交。

任一 SQL 错误、溢出或提交失败都会回滚整笔事务，标记不会写入；下一次服务启动会
重新尝试，而不会重复累计。标记已存在时只确认并启用账号钱包，不再执行汇总。
在旧 payload 的首次导入阶段，角色写入会临时保留旧 `wcoin`，直到该事务完成，防止
先清零再迁移的丢币窗口。

新账号创建也在一个事务中同时插入 `accounts` 与零余额 `account_wallets`，避免出现
可登录但没有钱包的账号。

## 修改点

- `src/server/mock_server_role.c`：钱包 schema、迁移标记、加载水合、账号赠送和商城
  联合事务；
- `src/server/mock_server_interaction_login.c`：商城三种购买路径统一使用联合钱包扣款；
- `src/server/mock_server_equipment_npc.c`：后台“加 W 币”改为账号钱包入账；
- `src/web_payment.inc.c`：充值余额、预留和回调入账改为账号钱包；
- `src/server/mock_server_transport.c`：确保在接受客户端前完成钱包迁移；
- `src/web_admin_server.c`：后台操作文案明确是账号 W 币。

## 验证边界

`make -j2` 的 C 编译成功；链接阶段因用户正在运行的
`bin/jh-online-server.exe` 占用目标文件而被 Windows 拒绝。没有停止、替换或重启用户
服务。下次由用户停止服务并重新构建/启动后，启动日志应出现一次：

```text
account_wcoin_wallet_migration marker=account-wcoin-wallet-v1 ... action=committed
```

后续启动不再汇总旧列。可用两个角色验证：给任一角色后台增加 W 币，切换角色后商城
余额应相同；任一角色购买后另一角色重新进入商城应显示扣减后的同一余额。
