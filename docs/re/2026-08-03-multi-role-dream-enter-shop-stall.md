# 多角色近同时进梦境：进本卡进度 / 梦境内出商城卡住

日期：2026-08-03

## 现象（用户报告）

角色 `1`（lxh001）、`455`（lxh002）、`456`（lxh003）接近同一时间进梦境副本；**后进的两个角色**不正常（用户 2026-08-03 23:44 澄清）：

1. 其一：遇怪后卡进度条；右上角倒计时数值不对，**应为 60**。
2. 其二：右上角倒计时在**进商城再退出后会刷新成 60**（旁证：出店路径会重新武装/纠正 HUD remain）。

先前「进本卡条 / 出店卡条」表述以本条为准；远端 `23.141.172.143` 无关，三人走本地服。

## 运行时状态快照（无当次 server stdout）

本地 `jh-online-server` 在调查时未在跑；`bin/server_out.txt` 停在 2026-08-01。
MySQL（127.0.0.1/`jh_online`）在 23:17 前后有写入：

| role | account | enter_daily(29*_01, 20260803) | account_roles.scene @ ~23:17–23:19 |
|------|---------|--------------------------------|-------------------------------------|
| 1 | lxh001 | enter_count=33, updated 23:17:47 | `29梦境空间_02` |
| 455 | lxh002 | enter_count=1, created/updated 23:17:36 | `c04临安府_06`（户外） |
| 456 | lxh003 | enter_count=2, updated 23:17:06 | `29梦境空间_01` |

对应关系假设（待日志确认）：

- **455**：进本请求已 `instance_enter_consume`（有日次），但持久场景仍户外 → 更像进本进度条卡住后断线/重登，或进本后立刻被出本写回户外。
- **456**：仍停在梦境 → 更像「已进本、出商城卡进度」一侧。

用户确认：三人进的是**本地端**（`127.0.0.1:19090` / 江湖归来），不是 `23.141.172.143`。

当次本机服务端以控制台 `--debug-log` 运行、**未重定向到文件**；`bin/server_out.txt` 仍停在 2026-08-01。调查开始时本机进程已停，23:17 当次 stdout 已丢。

2026-08-03 23:33 后本机重新拉起了 `jh-online-server.exe --debug-log`（pid 监听 19090/19091），仍无文件落盘。

## 已有相关契约（同日已修过的相邻根因）

| 文档 | 契约 |
|------|------|
| `2026-08-03-dream-enter-loading-bar-and-resource-stale.md` | drain 进 29* 后未 `mark_scene_ready` → poll clear `scene-mismatch` 整臂取消 → 卡条；已 `dream_enter_mark_ready` + mismatch hold |
| `2026-08-03-dream-exit-reenter-loading-gate.md` | 出本 settle clear 被传送 in-flight 挡住 / 过早 30/2 |
| `2026-08-01-shop-return-after-teleport-stale-hold.md` | 出店后 `direct_enter_pending` 等 stale 传送位 hold 住 post-catalog 30/2 → 过期无 ResetDownloadState |
| `2026-08-01-shop-open-cancels-mapstone-clear.md` | 开商城不得 cancel 未耗尽的 map_stone clear |

## 假设（未验证，禁止据此改业务）

### H1：三端近同时进本，账号 restore / sticky 下传送全局位串扰

`direct_enter_pending` / `last_scene_change_target_valid` 仍是进程全局工作区，靠 capture/restore 进账号快照。近同时进本若某次 poll/请求边界 restore 不完整，B/C 的 `map_stone`/`shop_return` clear 可能被 A 的传送位永久 hold，或被错误 cancel。

证伪/证实：远端日志里同一秒内交错的 `client=%08x` + `dream_bar_clear_arm` / `loading_clear_hold` / `shop_return_loading_clear_hold` / `stale_map_transfer_clear`。

### H2：进本 clear rem>0 时立刻开商城，双 clear 竞态

进 29* 武装 `map_stone_loading_clear`；出店再武装 `shop_return_loading_clear`。25/5 与 scene poll 优先 shop clear。若 map-stone rem 在 mmShop 期间被错误 cancel，或 shop clear 因 residual `direct_enter` 被 hold/drop，会出现「进本卡」或「出店卡」。

### H3：455 进本后被 TTL/禁组队 poll 立刻出本

TTL 默认 3600000ms，按角色绑定；三人各自进本应各自武装。仅当 455 误继承他人 expire 或错绑 roleId 才会秒出。日志关键字：`mock_dream_hud_ttl_exit` / `ttl_role_mismatch`。

## 首个偏离位置（目标）

仍 **unresolved**：缺 `role=455/456` 对应 `client=` 的 `dream_instance_enter` → `loading_clear_*` → `shop_return_*` 时间线。

不能把「卡住」本身当根因；需第一次错误状态是：

- clear 被 `scene-mismatch` cancel，还是
- clear 被 transfer/mmShop hold 到过期，还是
- 从未 arm clear，还是
- 响应投到了错误 session。

## 本轮最小取证目标

1. 本机 `--debug-log` 落盘（例如 `bin/server_out.txt`），覆盖三人近同时进本 + 其中一端梦境内进出商城。
2. 按 `client=` / `role=` 抽出 455、456 两条链，标出第一次偏离。
3. 复现步骤：三端近同时点使者进本；其中一端进本后立刻开商城退出。

## 续记：出店倒计时刷回 60 / 按角色分存（2026-08-03 23:50）

### 现象

后进角色梦境内倒计时已 <60，进商城退出后 HUD 又变成 60；先正常的角色能记住当前剩余。

### 根因

1. 桌面 HUD poke 的 `visit_ms` / `last_remain` 原是**进程全局**，未按 `roleId` 分槽；同进程换角会串。
2. `sce_load_entry` 把 mmShop 等噪声指针当场景名 → `dream_hud_visit_clear` → visit 清零 → 再 arm 29* 时 `fresh=1` → poke 回 60。  
   日志：`dream_hud_visit_clear sce=@w...` 后紧跟 `poke new=60`；未清时可见 `poke new=58`。

### 修复

- `main.c`：`visit_ms`/`last_remain`/`watch` 按 `roleId` 分槽（最多 8）；出梦境只清当前角色。
- 非法场景名（含控制字符）**忽略**，不再 clear。
- `network-client.c`：捕获标题 `WT 1/1/6` 的 `actorId` → `agent_dbg0b451a_note_active_role`。

### 验证

1. `make -j2`；重启各端 `main.exe --debug-log`。
2. 选角日志含 `dream_hud_active_role role=<id>`。
3. 进梦 → 等 remain 小于 60 → 进出商城：应 `L_POKE ... new=<剩余> role=<id>`，**不是** 60；无垃圾名 `dream_hud_visit_clear`。
4. 同进程换另一角色进梦：独立 visit，互不继承。

