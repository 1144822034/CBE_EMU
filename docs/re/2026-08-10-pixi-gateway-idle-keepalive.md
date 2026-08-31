# Gateway 侧稳定定时 scene-poll（2026-08-10）

## 问题

安卓 WebView 息屏后 `setInterval` 不可靠；服端 presence ~30s 无 poll 即 `heartbeat-timeout`。

## 方案

**稳定定时器放在 gateway 进程（Node `setInterval`）**，不依赖手机 JS：

1. 客户端进图后发出的 CBMS `SCENE_POLL`（flag=0x2）→ gateway **武装**该 WS。
2. 之后若客户端静默 ≥ ~2.2s，gateway 每 ~30s 用同一 `clientId` 代发 scene-poll → mock TCP（防踢余量：服端约 30s，与 keepalive 同量级，勿再拉长间隔）。
3. 响应帧仍推回该 WS；客户端 `LiveTransport.onPushBodies` 走 `handleBodies`。
4. 显式 `DISCONNECT`（登出）→ 解除武装，停 orphan。
5. **WS 异常断开**（未发 DISCONNECT）→ 进入 **orphan keepalive**：gateway 继续用该 `clientId` 代 TCP scene-poll，丢弃响应；宽限期内 mock 不因心跳踢人。客户端 `LiveTransport` 重连同 `clientId` 后任意包/SCENE_POLL 收回武装。

环境变量：

- `PIX_GATEWAY_KEEPALIVE_MS`（默认 **30000**；设 `0` 关）
- `PIX_GATEWAY_KEEPALIVE_IDLE_MS`（默认 2200）
- `PIX_GATEWAY_MOCK_TCP_TIMEOUT_MS`（默认 **60000**，单次 CBMS 等 mock 回包）
- `PIX_GATEWAY_ORPHAN_MS`（默认 **120000=2 分钟**；设 `0` 关；需 keepalive>0）

与 Mock 原有挂机（`2/10` hangup loop）无关，二者并存。

**2026-08-29**：曾默认关闭代发 / orphan；息屏保活仍由客户端 FGS + `__pixiNativeKeepAlive` 负责。

**同日补充**：orphan 关闭时，WS 异常断开（未收到客户端 DISCONNECT）→ gateway 排空该 socket 在途交换后，立刻向 mock TCP 发 CBMS `DISCONNECT`，触发 `client-disconnect` 落盘与 `explicit-disconnect` 下线，不再等 ~30s `heartbeat-timeout`。

**2026-08-30**：`ws-gateway.mjs` **代码默认**代发 30s + orphan 2 分钟（无需设环境变量）。仅要关闭时设 `PIX_GATEWAY_KEEPALIVE_MS=0` / `PIX_GATEWAY_ORPHAN_MS=0`。

## 部署

```bash
cd PixiJS_web && node scripts/ws-gateway.mjs
# 或 startGateway.bat / 重打后的 gateway-release/ws-gateway.exe
```

默认启动日志应出现：`idle scene-poll keepalive 30000ms` 与 `orphan keepalive after WS drop 120000ms`。

**改完需重启 gateway**。跑 exe 时须用当前源码重打（`npm run gateway:exe`），旧 exe 仍可能是默认关。

## 边界

- 宽限过期后停 orphan → 约 30s 内仍会被 `heartbeat-timeout`（避免永久幽灵在线）。
- 进程强杀 / 换新 `clientId`（整页刷新重建 Transport）不在 orphan 覆盖内。
- 同账号别处登录仍走 `account-login-takeover`。
