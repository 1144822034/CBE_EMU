# PixiJS 登录「未收到服务器列表 1/1/12」偶发失败

日期：2026-08-10

## 症状

PixiJS 登录频繁提示「未收到服务器列表（1/1/12…）」，多点几次才进服务器/角色列表。

## 证据

对本机 `ws://127.0.0.1:19092` → `:19090` 抓一次 `WT 1/1/12`：

- 客户端默认 / 拉取失败时发 `codeVersion=2`
- 服端 `server_code_version.txt=5`，门闩拒绝：
  - `result=2`
  - `information=请更新客户端`
  - **无** `serverinfo` 行
- 鉴权 / MySQL 不可用同样是 `result=2` + `information`（如 `account db unavailable`），也无服务器行

客户端 `fetchServerList` 只看 `servers.length===0`，把上述**真实失败应答**误报成「未收到列表」。

复现命令：

```bash
cd PixiJS_web
npx tsx scripts/debug-serverlist-once.ts
# 旧行为：codeVersion 回落 2 → 请更新客户端
# 修复后：codeVersion=5；若 MySQL 不可达则 information=account db unavailable
```

`codeVersion=5` 后版本门闩通过；账号库不可达或错误账号时仍为 `result=2`（仍无 server 行），这也能解释「多试几次才进」——MySQL/鉴权偶发失败。

## 根因

1. **版本契约**：`loadClientCodeVersion` 只 `fetch("/client_code_version.txt")`，在 `vite base: './'` / Capacitor 下易 404，回落硬编码 `2`；服端期望 `5` → `result=2`。
2. **错误归属**：收到 `1/1/12 result=2` 后仍抛「未收到服务器列表」，掩盖「请更新客户端」/鉴权/MySQL 文案。
3. **加重因素**：重复点击登录会 `transport.close()` 打断在途交换；gateway TCP `pull()` 一次性 `data` 监听与 localhost 快回包竞态可能丢 CBMR；`server_login_servers` / 账号校验依赖 MySQL，库抖动时同样无 `serverinfo`。

## 修改

- `bundledCodeVersion.ts` + `sync-offline-assets.mjs`：打包内嵌当前 `codeVersion`；拉取优先相对路径，失败用内嵌值（不再静默回落 2）。
- `GameSession.fetchServerList`：`result=2` / `information` 直接抛「登录失败：…」。
- `main.ts`：`loginInFlight` 防重入。
- `Transport.ts`：WS 收包 inbox，避免 follow-up 丢帧。
- `ws-gateway.mjs`：TCP 连续缓冲，去掉 `pull` 内一次性 `data` 竞态。

## 验证

1. `npx tsx scripts/debug-serverlist-once.ts` → `codeVersion:5`；不再因版本门闩被拒。
2. 浏览器/APK 登录：失败应见「登录失败：请更新客户端 / account db unavailable / …」，不再笼统「未收到服务器列表」。
3. MySQL（`CBE_MYSQL_*` → `jh_online`）可达且账号正确时，一次进入角色列表；连点确定不应并行开两路登录。
4. 若仍偶发且文案为 `account db unavailable`：查服端 MySQL，而非客户端收包。
