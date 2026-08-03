# 梦境进本进度条卡住 + 地图更新不自动拉取

## 现象（2026-08-03）

1. 更新梦境地图后，使者进本慢，提示「地图加载中，请稍候再进入」。
2. 本地 `JHOnlineData` 与 web 发布版本不一致时不自动更新。
3. 手动删本地地图后，需先进商店再退出才能进副本。
4. 进副本后进度条一直不消。

## 运行时证据（debug-0b451a）

户外→梦境：

```text
dream_instance_enter from=c04临安府_06 to=29梦境空间_01 drain=1 clear_rem=3
download_busy_clear reason=dream-df-screeninit-complete
loading_clear_cancel reason=scene-mismatch clear=29梦境空间_01 visible=c04临安府_06 rem=3
```

同会话内已在梦境再进时：`loading_clear_ok rem=2→1→0`（visible 已是 29*）。

「地图加载中」对应 `instance-enter-gate-wait-clear`：户外 `map_stone_loading_clear` 仍 armed（含 mmShop hold rem=3）时拒绝 29*  hop。商店进出会放行 poll clear，故表现为「必须进商店」。

## 根因

1. **进度条**：drain 进梦境武装了 29* poll `30/2`，但跳过 WT 2/3，未 `mark_scene_ready`；`sceneVisible` 仍为临安 → clear 被 `scene-mismatch` 整臂取消，`ResetDownloadState` 耗尽失败。
2. **资源不更新**：客户端 `uptodateMemo` 对已探测过的本地 content-hash 直接跳过 WT18/7；后台发布后本地字节未变，memo 永久命中，永不重拉。

## 修复

- `mock_server_scene_sync.c`：29* instance enter 后 `mark_active_session_scene_ready_from_role`。
- `mock_server_social.c`：29* clear vs 户外 visible 时 hold，不 cancel。
- `main.c` / Android `cbeEmu/main.c`：对 `should_version_check_existing` 资源禁用 memo 跳过。

## 验证

1. `make -j2`；重启 server + 客户端。
2. 后台改梦境刷怪并保存 → 不删本地直接点使者进本。
3. 期望：`dream_resource_version_probe` / `dream_update_chunk`（或 version 变化后的下载）；`dream_enter_mark_ready`；`loading_clear_ok` rem→0；**无** `loading_clear_cancel scene-mismatch`；进度条消失可走。
4. 回归：户外地图石、商店进出、梦境→临安→再进。

## 续记：多刷怪点互相覆盖（同日）

**根因**：梦境保存把每个点 nearest-snap 到 inject 三槽 `(135,49)/(65,84)/(107,254)`，不同 admin 坐标常落到同一槽 → SCE/绑定 `(x,y)` 覆盖（日志：`real=203` 与 `204` 同写 `107,254`）。

**修复**：取消三槽写死；保存原样使用管理后台坐标（可达性由后台手调）。日志：`dream_spawn_keep_admin`。
