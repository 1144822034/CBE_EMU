# 临安传送后场景加载不完成（2026-07-26）

## 触发条件

账号 `21642502` 从 `00蓬莱仙岛_02.sce` 使用传送石的第 47 个出口进入
`c04临安府_01.sce`，解析的落点为 `(201,140)`。

## 预期与实际

传送石直达路径先由延迟场景事件下发带坐标的 `WT30/1`。随后客户端的
`WT2/3` 是该新场景壳的完成请求，服务端应在这一个响应末尾发送不带
`posinfo` 的成功 `WT30/2`，让客户端的 `sub_1039770` 调用
`ResetDownloadState`，但不会再次进入场景。

实际服务端只将 `c00蓬莱仙岛_01.sce` 识别为这个完成契约。临安虽然已有同一
个传送石直达来源、相同待确认目标和已经选中的目标场景，却被派发至泛化
`scene-change-combo` 分支。该分支记录了
`mock_scene_change_teleport_resource_pending`，把 `30/2` 延后到下一条
`WT6/1`。客户端因此在 `2/3` 的完成阶段没有关闭加载状态。

## 证据

`bin/local-service-current.out.log` 的复现会话（client `6d0c0c67`）依序记录：

1. `mock_scene_landing_resolve scene=c04临安府_01.sce ... landing=(201,140)`；
2. `mock_teleport_stone_confirmed_exit_combo ... deferred_scene=1`；
3. `mock_scene_change_teleport_resource_pending ... completion=defer-30/2-until-WT6/1`；
4. 下一条场景资源请求才收到含 `30/2` 的 718 字节响应。

在第 2 和第 3 步之间没有任何实际 `WT18/7` 请求或完成日志，故不能把这条
`WT2/3` 视为“正在下载资源”的通用路径。动态 NPC 的四个 actor/GIF 都存在，
并且客户端的 NPC 目录上限恰为四；它们不是这次首次加载停滞的第一偏离点。

反编译和既有运行证据表明 `WT30/2` 的无坐标成功确认会调用
`ResetDownloadState`，而不会触发第二次 `EnterSceneByMapName`：
`docs/re/2026-07-20-teleport-remote-observation-order.md`、
`docs/re/2026-07-22-teleport-resource-completion-order.md`。

## 根因与修复

根因是 `vm_net_mock_is_current_scene_completion_request()` 把传送石直达完成
契约错误地限制为铜雀台地图。修复将条件改为该契约真正拥有的三项状态：
传送石直达仍待完成、目标与保存目标精确一致、且当前场景就是该目标；不再以
地图名分支。资源下载目标仍由 `target.needsSceneDownload` 排除，避免在真正的
`WT18/7` 期间提前发送 `30/2`。

## 验证

- `php tmp/teleport-resource-completion-regression.php 19090` 通过：在延迟
  `30/1` 之后，完整 `WT2/3 + 27/11 + 27/4 + 7/42` 完成请求收到一次 `30/2`；
  随后的 `WT6/1` 不重放它。
- `php tmp/tongquetai-direct-teleport-completion-regression.php 19090` 通过：
  铜雀台仍在 `WT2/3` 关闭加载状态、将非空 NPC 目录留给后续 `WT6/1`。
- `make -j2` 通过；新的 `jh-online-server.exe` 已重启并监听
  `127.0.0.1:19090` 与 `127.0.0.1:19091`（PID `58812`，启动 stderr 为空）。

仍需用本地客户端重复“蓬莱仙岛_02 -> 临安”路径，确认加载遮罩消失、落点保持
`(201,140)`，并覆盖实际触发 `WT18/7` 资源下载的首次到达与重复传送。
