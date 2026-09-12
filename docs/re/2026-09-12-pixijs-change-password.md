# Pixi 游戏内修改密码

## 目标

菜单「系统设置 → 修改密码」打开与注册页同风格的三栏表单（旧密码 /
新密码 / 确认新密码），经 gateway 调用账号中心 `POST /user/password`。

## 契约

与网页账号中心改密同一路径：`vm_mock_service_account_verify_credentials` +
`vm_mock_service_account_set_password`。

| 层 | 路径 | 说明 |
|---|---|---|
| Admin | `POST /user/password` | 网页：会话 cookie + HTML 跳转；Pixi：表单带 `account` → JSON |
| Gateway | `POST :19092/user/password` | 代理到 admin `:19091` |
| Pixi | `src/net/password.ts` | 本地校验后 fetch；成功 `{ok:true,account}` |

表单字段：`account`、`current_password`、`new_password`、`confirm_password`。

校验：当前密码正确；两次新密码一致；新密码 6–63 位。成功后使该账号其他网页会话失效。

## 客户端流程

1. 世界菜单 → 系统设置 → 修改密码（需已登录账号）
2. 界面预填账号提示；输入旧/新/确认 → 确定 → `changeAccountPassword`
3. 成功：更新 `session` / `localStorage` 凭证，返回世界并 toast
4. 返回：不改密，回世界

## 修改点

- `src/web_admin_server.c`：`/user/password` JSON 模式（带 `account`）
- `PixiJS_web/scripts/ws-gateway.mjs`：proxy `/user/password`
- `PixiJS_web/src/net/password.ts`、`TitleScreen.ts`、`LoginIme.ts`、`MenuScreen.ts`、`main.ts`

## 运维注意

`not-found` 来自**旧版** gateway（无 `/user/password` 代理）。部署后需：

1. 重启 `node scripts/ws-gateway.mjs`（或重打 `gateway:exe`）
2. 重启含改密 JSON 模式的 `jh-online-server`（admin `:19091`）

本机验证：`POST http://127.0.0.1:19092/user/password`（含 `account` 等字段）应返回 JSON。
