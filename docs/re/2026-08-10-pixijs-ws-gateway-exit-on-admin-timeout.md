# PixiJS ws-gateway 偶发自行退出

日期：2026-08-10

## 症状

`npm run gateway` / `ws-gateway.exe` 运行一段时间后进程消失，WS `:19092` 不再监听；无显式 `process.exit`。

## 证据

最小复现（与 `proxyAdminApi` 相同结构）：

1. upstream `timeout` → `destroy()` → 先 `writeHead(504)`
2. destroy 再触发 `error`（`socket hang up`）→ 再 `writeHead(502)`
3. Node 抛 `Cannot write headers after they are sent to the client`
4. 该异常在 `http.ClientRequest` 事件回调里**无 try/catch** → `uncaughtException` → 进程退出

触发面：Pixi 经 gateway 访问的 admin 代理：

- `GET /api/monster-levels`（进图拉等级覆盖）
- `POST /api/recharge/create`

仅当 admin（默认 `:19091`）**已接通但 15s 内无完整响应**时走 timeout 路径；立刻 `ECONNREFUSED` 只打一次 502，不崩。故表现为偶发。

## 根因

`ws-gateway.mjs` 的 admin HTTP 代理在 `timeout` 与随后的 `error` 上各写一次响应，违反「每个 HTTP 响应只结束一次」契约；异常未在代理层收敛，整桥退出。

## 修改

- `proxyAdminApi`：`responded` / `headersSent` 守卫，timeout 与 error 只回复一次。
- WS `message` chain 补 `.catch`；`ws` / `server` / `wss` 挂 `error` 日志。
- 进程级 `uncaughtException` / `unhandledRejection` 只记日志，避免未知边缘再默默杀桥。
- follow-up CBMR：超时直接 `cancelPull`，去掉 `Promise.race` 旁路。

## 验证

1. 对卡住的 admin 打 `GET /api/monster-levels`：应 504，gateway 进程仍在、`/healthz` 仍 200。
2. admin 正常时 monster-levels / 充值代理仍 200。
3. 重启 gateway 后进户外图、断线重连，WS 交换不受影响。

## 仍未知

- 若线上还有无 HTTP 代理参与的纯 WS 退出，需保留当时控制台最后几行（现会有 `uncaughtException` / `unhandledRejection` 前缀）。
