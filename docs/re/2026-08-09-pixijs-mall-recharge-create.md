# PixiJS 商城接入账号中心充值创单

日期：2026-08-09

## 需求

PixiJS_web 商城内输入金额后直接创建 W 币充值单据；APK 创单成功后用
Intent 打开支付平台返回的 `payUrl`，唤起支付宝或微信完成付款。不复用账号中心
HTML 充值页，只复用服务端创单/验签/入账权威逻辑。

## 契约

- 创单权威仍在 `vm_mock_payment_create_order`（`web_payment.inc.c`），密钥不进客户端。
- 新 JSON API：`POST /api/recharge/create`（admin `:19091`）
  - Body（form-urlencoded）：`account` `password` `role_id` `yuan` `pay_type`
- 鉴权：先按 UTF-8 / GBK 校验账号密码（游戏 WT 多为 GBK 入库）；失败则校验
  `role_id` 是否属于该账号（UTF-8/GBK），供已登录 Pixi 商城创单。
  - `pay_type`：`1` 微信，`2` 支付宝
  - 成功：`ok/payId/payUrl/reallyPriceCents/wcoinAmount/...`
  - 失败：`{"ok":false,"error":"..."}`（中文安全文案）
- Gateway（`:19092`）代理同路径到 admin；公网只开 gateway 时 Pixi 可走
  `http(s)://gateway-host:19092/api/recharge/create`。
- 商城 UI：底栏「酷宝 N」旁可点「充值」→ `prompt` 金额 → 选择支付宝/微信 → 创单。
- APK 支付呈现（V免签 = **个人收款码**，不是商家码）：
  - 支付宝 `https://qr.alipay.com/fkx…`：`ACTION_VIEW` 打开（无 setPackage），
    **同时**弹扫码层双保险。勿对本机相册识自己的码（易开「名片」）。
  - 微信 `wxp://`：二维码 + 本机相册识码（无打开链接）。

## 修改

1. `src/web_admin_server.c`：`/api/recharge/create` OPTIONS/POST。
2. `PixiJS_web/scripts/ws-gateway.mjs`：POST/OPTIONS 代理。
3. `PixiJS_web/src/net/recharge.ts`、`openPayUrl.ts`；`SystemPanel` 底栏入口；
   `main.ts` 编排创单与打开支付。
4. `PayLauncherPlugin.java` + `MainActivity.registerPlugin`。

## 验证

- `make -j2`；`PixiJS_web` 下 `npx tsc --noEmit`。
- 须重启/重建 gateway（含 HTTP 代理），旧纯 WS gateway 无此路径。
- 手测：商城 → 充值 → 金额 → 选渠道 → 服务端 `[payment] order_created` →
  APK 跳转支付 App；入账后重开商城酷宝增加。

## 风险

- Android 11+ 若 OEM 限制隐式 VIEW，可能 Toast「无法打开支付」；此时订单已创建，
  可在账号中心订单页继续支付。
- 支付平台 `reallyPrice` 必须与付款金额完全一致，否则无法自动入账（既有契约）。
- 微信 V免签 `payUrl` 多为 `wxp://`（与账号中心一致：仅作二维码载荷，无「打开链接」）。
  同机无法摄像头扫本机屏幕：APK 将二维码存入相册并打开微信，引导
  「扫一扫 → 相册识别」；按钮「保存到相册并打开微信」可重试。
- 支付宝个人码：Intent 打开 + 扫码双保险；勿对本机相册识码（易开「名片」）。
