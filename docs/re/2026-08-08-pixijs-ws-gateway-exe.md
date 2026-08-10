# PixiJS ws-gateway 打 Windows exe（2026-08-08）

## 目标

把 `PixiJS_web/scripts/ws-gateway.mjs`（WS ↔ TCP CBMS 桥）打成单文件 `ws-gateway.exe`，在无 Node 的机器上直接跑。

## 成功方案：esbuild 打包 + Node SEA + postject

本机 Node **v22.22.0**。不要用 `@yao-pkg/pkg`：拉取 `node-v22-*-win-x64` 预编译失败后会改从源码编 Node，极慢且易卡住。

### 命令

```bash
cd PixiJS_web
npm run gateway:exe
```

产物：`PixiJS_web/gateway-release/ws-gateway.exe`（约 82MB，内嵌本机 `node.exe`；独立于 vite `dist/`，Android 出包不会清掉；`gateway-release/` 已 gitignore）。

### 流水线（`scripts/build-gateway-exe.mjs`）

1. **esbuild**：`ws-gateway.mjs` + 依赖 `ws` → 单文件 CJS `gateway-release/ws-gateway.cjs`  
   - `platform: node`、`format: cjs`、`packages: "bundle"`  
   - SEA 入口用 CJS 最稳；项目本身是 `"type": "module"`，不要直接喂 `.mjs` 给 SEA。
2. **SEA blob**：写 `sea-config.json`，执行  
   `node --experimental-sea-config sea-config.json`  
   → `sea-prep.blob`（可设 `disableExperimentalSEAWarning: true`）。
3. **复制二进制**：`copyFileSync(process.execPath, ws-gateway.exe)`。
4. **去签名（可选）**：官方 Windows Node 带 Authenticode；有 `signtool` 时 `signtool remove /s ws-gateway.exe`。本机无 SDK 时跳过，postject 会警告 `signature seems corrupted`，**exe 仍可跑**。
5. **注入**：`postject ws-gateway.exe NODE_SEA_BLOB sea-prep.blob --sentinel-fuse NODE_SEA_FUSE_fce680ab2cc467b6e072b8b5df1996b2`  
   - fuse 字符串固定，与当前 Node SEA 文档一致，勿改。

### 运行参数（与 `npm run gateway` 一致）

```bat
ws-gateway.exe
ws-gateway.exe 19092 127.0.0.1 19090 0.0.0.0
```

| 位置 / 环境变量 | 含义 | 默认 |
|-----------------|------|------|
| argv[1] / `PIX_GATEWAY_PORT` | WS 监听端口 | 19092 |
| argv[2] / `PIX_TCP_HOST` | mock TCP 主机 | 127.0.0.1 |
| argv[3] / `PIX_TCP_PORT` | mock TCP 端口 | 19090 |
| argv[4] / `PIX_GATEWAY_HOST` | WS bind | 0.0.0.0 |

SEA 下 `process.argv` 仍可用：冒烟时 `ws-gateway.exe 19093 127.0.0.1 19090 127.0.0.1` 正确打印 `listen 127.0.0.1:19093`。

### 依赖（devDependencies）

- `esbuild` — 打 CJS 包
- `postject` — 往 node.exe 注入 SEA blob  
- 运行时仍只需原脚本依赖 `ws`（已打进 CJS）

## 已排除

| 方案 | 结果 |
|------|------|
| `@yao-pkg/pkg` + `node22-win-x64` | 远程 cache miss → 下载源码编 Node，不可接受 |
| 直接 `pkg` 喂 ESM `.mjs` | 与 `"type":"module"` / ESM 入口不友好；即便成功也依赖 pkg 的 Node 基座 |

## 分发注意

- exe ≈ 完整 Node 体积，属预期。
- 未重新签名时，SmartScreen / 企业策略可能拦截；内网自用可忽略，对外分发需自签或装 Windows SDK 后再签。
- 防火墙放行 **19092**（及所选 WS 端口）；TCP 侧仍要本机/同机 `jh-online-server` `:19090`。

## OS 下限（实测）

- **最低：Windows 8 / Server 2012**（`GetSystemTimePreciseAsFileTime`）。
- **Windows Server 2008 R2 / Windows 7 不能跑**这份 Node22 SEA exe，启动即报：  
  `无法定位输入点 GetSystemTimePreciseAsFileTime 于动态链接库 KERNEL32.dll`。
- 实务：游戏服可留在 2008 R2；**gateway 放到 Win10/11（或任意 ≥2012）机器**，TCP 指回 2008 R2：

  ```bat
  ws-gateway.exe 19092 <2008r2的IP> 19090 0.0.0.0
  ```

  客户端连该 gateway 的 `ws://<gateway机IP>:19092`。若必须 gateway 与服同机在 2008 R2，需另做非 Node22 方案（Go/C# 或极老 Node），当前流水线不做。

## 相关文件

- 源：`PixiJS_web/scripts/ws-gateway.mjs`
- 打包：`PixiJS_web/scripts/build-gateway-exe.mjs`
- npm：`gateway` / `gateway:exe`
- 客户端连网：`PixiJS_web/src/net/Transport.ts`
