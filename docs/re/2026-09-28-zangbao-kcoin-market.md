# 藏宝阁 + 酷币交易/提现

日期：2026-09-28

## 概述

独立货币「酷币」（`account_roles.kcoin`，与酷宝 `wcoin` 分离）。玩家在游戏内藏宝阁以酷币标价寄售任意背包物品；买家经赞助接口付人民币；支付成功后卖家到账 `floor(price * 95 / 100)` 酷币，物品进入买家背包（满包则进待领取 claim）。酷币满 50 且为 10 的倍数可申请提现：申请时原子扣币，须先上传微信/支付宝收款码；管理后台审核同意结单，拒绝退币。

## 汇率

`server_kcoin_config.kcoin_per_yuan`（默认 1：1 元 = 1 酷币）。
赞助金额：`yuan = ceil(price_kcoin / kcoin_per_yuan)`，且受支付配置 `minimum_yuan`/`maximum_yuan` 约束。

## 表

- `account_roles.kcoin`
- `server_kcoin_config`
- `server_zangbao_listings`（0 active / 1 reserved / 2 sold / 3 cancelled）
- `kcoin_market_orders`（V免签赞助单，settled 入卖家酷币而非买家 wcoin）
- `server_zangbao_claims`
- `account_role_kcoin_payee`
- `kcoin_withdraw_orders`（0 待审 / 1 同意 / 2 拒绝）

迁移：`server/mysql/migrate_add_kcoin_zangbao.sql`（运行时 `ensure_schema` 也会建表）。

## HTTP API（经 gateway `:19092` → admin `:19091`）

玩家（account/password/role_id）：

- `GET /api/zangbao/browse|mine`
- `POST /api/zangbao/list|cancel|buy`
- `GET /api/zangbao/order`
- `GET /api/kcoin/balance`
- `POST /api/kcoin/payee/upload`（`image_b64` + `pay_channel` 1/2；客户端先压 JPEG，admin 请求缓冲 512KiB）
- `POST /api/kcoin/withdraw/create`
- `GET /api/kcoin/withdraw/list`

支付回调：`/payment/cbhub/notify|return` 先验签，再 `vm_mock_zangbao_try_settle_verified`（纯 SQL 事务入卖家酷币 + claim；**禁止**在事务内 `role_db_save`，否则嵌套 `START TRANSACTION` 会提前提交，V免签补单看到 `error_sign`）；非市场单则走酷宝充值结算。

## Admin

Tab「藏宝阁」：汇率/开关；提现列表；管理前缀下 `/kcoin-qr?id=` 看收款码（页面 rewrite 后为 `/admin-…/kcoin-qr`）；同意 / 拒绝（退币）。

## 客户端

娱乐菜单「藏宝阁」→ `ZangbaoPanel`（浏览/我的/上架/酷币）。购买走微信赞助 `openPayUrl`。

## 关键文件

- `src/web_zangbao.inc.c` / `src/web_admin_zangbao.inc.c`
- `src/web_payment.inc.c`（settle 钩子）
- `src/server/mock_server_role.c` / `mock_server_core.c`（kcoin 字段）
- `PixiJS_web/src/ui/ZangbaoPanel.ts` / `net/zangbaoApi.ts`
- `PixiJS_web/scripts/ws-gateway.mjs`
