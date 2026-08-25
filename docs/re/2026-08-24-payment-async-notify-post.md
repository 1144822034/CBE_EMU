# 支付异步通知 POST 兼容

状态：已实现，待部署包含本改动的服务端。

## 原始失败链路

监控 App 对订单通知地址 `/payment/cbhub/notify` 主动提交异步结果。原入口把通知地址和
同步浏览器返回地址合并处理，并只接受 `GET`：任何 `POST` 都会在 `web_admin_server.c` 的路由
层直接返回 `405 Method Not Allowed`、`Allow: GET` 和正文 `fail`。请求没有进入
`vm_mock_payment_parse_callback()`，也没有执行签名或订单校验，因此支付方会判定“异步通知失败”。

## 修复后的契约

- `/payment/cbhub/notify`：接受传统 `GET` 查询参数，或
  `POST application/x-www-form-urlencoded` 正文；两者共享完全相同的字段、签名和订单校验。
- `/payment/cbhub/return`：保持 `GET` 专用，避免浏览器返回页面意外接受任意 POST。
- 必填字段为 `payId`、`param`、`type`、`price`、`reallyPrice`、`sign`。
- 签名输入严格为
  `payId + param + type + price + reallyPrice + secretKey`，无分隔符。`price` 与
  `reallyPrice` 的原始文本参与签名；例如 `1.00` 与 `1` 的签名不同。
- `X-Forwarded-For`、来源 IP 和浏览器状态不参与支付授权；唯一授权依据是通讯密钥签名和本地
  订单的 `payId`、`param`、支付方式、标价匹配。
- 通知成功或已支付待入账时返回 HTTP 200 和 `success`；无效通知保持 HTTP 400 和
  `error_sign`，不向外部暴露具体失败原因。

## 可观察性

回调日志现在带 `source=notify-get` 或 `source=notify-post`，以及不含密钥的原因：

- `payment-config-unavailable`
- `callback-fields-invalid`
- `signature-rejected`
- `order-rejected-or-credit-failed`
- `credited` / `paid-pending`

监控端的重试排查先看这条日志。若方法已是 POST 但仍失败，优先核对请求体是否为表单编码、
字段名称和金额文本是否与签名输入一致，以及 `payId` 是否由本服务实际创建而非测试构造。

## 验证

- `make -j2` 已成功完成。
- `admin-request-length-regression` 已编译并运行到新增的支付 POST 用例：它确认表单字段可由
  既有回调解析器读取、`POST /payment/cbhub/notify` 选取正文并标记为 `notify-post`，而
  `POST /payment/cbhub/return` 仍会被拒绝。该用例之后，整套既有后台回归停在无关的称号预览
  渲染断言 `designation directory or badge preview rendering failed`；因此不能将其报告为整套
  管理后台回归全绿。
