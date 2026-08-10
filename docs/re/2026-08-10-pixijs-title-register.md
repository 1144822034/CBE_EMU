# Pixi 登录页注册入口

## 目标

标题菜单保持 `title2.gif`（含「江湖充值」）。注册入口放在**账号登录**页，经
gateway 调用管理端注册契约创建账号，成功后自动登录。

## 契约

与用户前台 `/user/register` 同一创建路径：`vm_mock_user_valid_registration` +
`vm_mock_service_account_create_record`。

| 层 | 路径 | 说明 |
|---|---|---|
| Admin | `POST /api/register` | `application/x-www-form-urlencoded`：`account`、`password` |
| Gateway | `POST :19092/api/register` | 代理到 admin `:19091`（同 recharge） |
| Pixi | `src/net/register.ts` | 本地校验后 fetch；成功 `{ok:true,account}` |

校验（与网页一致）：账号 4–32 位 `[A-Za-z0-9_]`；密码 6–63 位。

## 客户端流程

1. 菜单「登陆游戏 / 开始游戏」→ 登录页；点「没有账号？点此注册」→ `register` 模式（账号 / 密码 / 确认密码）
2. 两次密码须一致；确定 → `registerAccount` → 成功则 `setCredentials` + `doLogin`
3. 注册页返回 → 登录页；菜单「江湖充值」仍提示账号中心

## 修改点

- `src/web_admin_server.c`：`/api/register`
- `PixiJS_web/scripts/ws-gateway.mjs`：proxy
- `PixiJS_web/src/net/register.ts`、`TitleScreen.ts`、`main.ts`

## 运维注意

`not-found` 来自**旧版** gateway（无 `/api/register` 代理）。部署后需：

1. 重启 `node scripts/ws-gateway.mjs`（或重打 `gateway:exe`）
2. 重启含 `/api/register` 的 `jh-online-server`（admin `:19091`）

本机验证：`POST http://127.0.0.1:19092/api/register` 应返回 `{"ok":true,"account":…}`。
