# 比武 SETTLED 后游戏服务仍卡 / 管理后台 503

日期：2026-08-19  
环境：`192.1.1.3` / MySQL `jh_online` / 管理端口 `19091` / 游戏端口 `19090`

## 症状

1. 今日比武（session_id=7）DB 已为 `status=3`（SETTLED），25 人记分。
2. 管理后台间歇性返回：`游戏服务繁忙，管理操作请稍后重试。`（HTTP 503，约等满 8s）。
3. 游戏 ping（CBMS PING）可达，但与其他请求争用 `g_vm_mock_service_protocol_mutex` 时明显变慢。

## 运行时证据

| 探测 | 结果 |
|------|------|
| `server_arena_sessions` #7 | `20260819`, slot=0, **status=3** |
| `server_arena_scores` | 25 行；多角色 score=200 |
| 仍在擂台 scene 的角色（DB） | 约 6 |
| `http://192.1.1.3:19091/` ×5 | 2×200（0.87s / 8.08s），3×503（~8.1s） |
| MySQL `PROCESSLIST` | 无长时间阻塞查询 |
| 远程运维 | **MySQL** `192.1.1.3:3306` 用户 `root`/`123456`（允许 `192.1.1.%`）可连；**Windows 游戏进程**需 RDP/本机操作，上述为库账号非系统 SSH |

503 文案来自 `web_admin_server.c`：`protocol_lock_timeout` 抢不到协议锁（等待 8s）。

## 根因

1. **线上进程仍是旧二进制**：工作区 `mock_server_arena.c` 含 2026-08-18「比武期间全服卡」修复（`ensure_session` 热路径不每秒查库、SETTLED 不全表 announce、`deferred` 匹配等），**尚未部署到 192.1.1.3**。
2. **SETTLED 后仍跑 arena tick 重逻辑**：旧逻辑在 `matchEnd+120min` 窗口内每秒于 scene-sync poll（持协议锁）执行 reconcile、venue sync、64 槽 duel 扫描等；多客户端 poll 排队 → 管理后台 8s 超时。
3. **SETTLED 后仍强制 arena HUD poll body**：仍在擂台地图的客户端每 poll 走 `poll_needs_body`，放大锁占用（Pixi 顶栏只需一次 status=3 清 HUD）。

MySQL 侧当时空闲，不是 InnoDB 锁；是**服务端协议锁**被比武 tick 占满。

## 本次代码修改（待部署）

文件：`src/server/mock_server_arena.c`, `mock_server_equipment_npc.c`

1. **`arena_tick`**：`status==SETTLED` 时直接 return（保留 wager tick），不再 reconcile / duel-timeout 扫描。
2. **`arena_settle`**：结算前 `arena_settle_abort_duels` 强制结束未完结的 `arenaMode` 决斗（不加分）。
3. **`arenaHudSettlePushed`**：每个客户端 SETTLED HUD（41/5 status=3）只推一次，`poll_needs_body` 之后走 idle 快路径。

已 `make -j2` 通过，产物：`bin/jh-online-server.exe`。

## 部署与验证（需在 192.1.1.3 或内网跳板执行）

1. 停旧 `main.exe` / `jh-online-server.exe`。
2. 拷贝新 `bin/jh-online-server.exe`（及依赖）到服务器，按原 `start.bat` 或 systemd 启动。
3. 验证：
   - [ ] 管理后台刷新应 **<1s**，不应再连续 503
   - [ ] 日志无持续 `arena_tick_slow` / `protocol_lock_timeout`
   - [ ] 仍在擂台的客户端顶栏「比武 比赛」应被清掉；排行/领奖正常
4. 若仍 503：查 stdout 是否出现 `protocol_lock_holder holder=... hold_ms=...` 继续取证。

## 仍未知

- 192.1.1.3 SSH 关闭，未能直接 `tail` 线上 `protocol_lock_holder` 日志确认占锁函数名。
