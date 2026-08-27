# 登录失败限频（同 IP 或 同账号 60s）

Date: 2026-08-23

## 背景

公网 gateway 暴露后，日志出现大量 `login_reject ... account or password error`（连续账号爆破）。mock 经 gateway 看到的 TCP 源地址均为 `127.0.0.1`，必须在 CBMS meta 中附带真实客户端 IPv4。

## 契约

| 项 | 行为 |
|----|------|
| Gateway | 在转发 CBMS 时于 meta 尾部追加 `0xFE` + IPv4 LE（5 字节） |
| Mock | `parse_account_metadata` 解析 `clientIpv4`；认证登录路径限频 |
| 冷却 | **60s**（`VM_MOCK_LOGIN_FAIL_LIMIT_COOLDOWN_MS`） |
| 粒度 | **同 IPv4 或 同 userName**（OR）：任一维度上次失败后 60s 内拒绝 |
| 防代理切换 | 换 IP 仍受账号冷却；换账号仍受 IP 冷却 |
| 客户端文案 | 仍返回 `account or password error`（不暴露限频） |
| 日志 | 冷却期内首次 `rate-limited` 打一条；同窗口后续静默计数；冷却结束或新失败时打 `login_reject_rate_limited_summary suppressed=N`；冷却期内跳过 `mock_code_version_*` |
| 成功登录 | 清除该 IP 与该账号的冷却 |
| 游客/无账号登录 | 不限频 |

## 部署

1. 重新编译 `jh-online-server.exe` 并重启 mock。
2. 重启 gateway（`npm run gateway` 或重打 `ws-gateway.exe`）。
3. 启动日志应有 `login_fail_limit_ms=60000`。

## 验证

1. 同一 IP 对账号 A 失败后，换账号 B 同 IP：应 `ip_hit=1` 被限频。
2. 账号 A 失败后，换 IP 再试账号 A：应 `user_hit=1` 被限频。
3. 等待 60s 后再试：应恢复为普通 `account or password error` 或成功。
4. 正确密码登录成功后，该 IP 与该账号冷却应被清除。
