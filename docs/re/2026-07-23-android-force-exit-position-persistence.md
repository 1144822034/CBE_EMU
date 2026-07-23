# Android 强制退出后重登的位置恢复

## 状态

已实施并以跨服务端重启的协议回归验证。该问题不能沿用
`2026-07-23-unstuck-scene-authority.md` 的修复：两者的首个客户端请求和服务端状态
边界不同。

## 可复现现象

Android 客户端在场景中移动后被系统强制终止；再次启动、认证并选择同一角色后，
角色有时回到出生点。

若设备实测仍出现重置，需补充其原始服务端日志：旧 client id 的最后一条 `2/1
moveinfo`、是否出现 `CBMS flags=0x4`、新 client id 的登录/选角，以及选角响应使用的
`scene/pos_x/pos_y`。这将用于排除账号标识或选角前 scene target 的另一条偏离，不能再
将其归为本问题后以出生点兜底。

## 已排除：不是“脱离”有符号 exitID 分流错误

此前场景内“脱离”复现的请求是：

```text
WT 1/16/3 { exitID: typed-u32 0xffffad3c, type: 0 }, 1/27/11, 1/7/42
```

`JianghuOL.CBE:scene_runtime_init_and_sync (0x01012FB4)` 会为该场景运行时路径
构造 `1/16/3`；旧服务端把符号扩展的 i16 `exitID` 误认作石头传送，随后错误写入出生
场景。该路径已经在 `2026-07-23-unstuck-scene-authority.md` 修正并以 5 种 runtime
stream 变体回归。

Android 强制杀进程不会产生 CBE 的场景运行时恢复请求，因此没有该 `1/16/3` 分流点。

## 当前链路与证据

1. `JianghuOL.CBE:scene_runtime_tick (0x01014EE0)` 将最多十个本地方向码汇成一个
   `2/1 moveinfo` 上传；方向消费的客户端契约仍由
   `scene_node_update_move_blob (0x01012A76)` / `ProcessSceneAutoAction (0x01045428)`
   处理。
2. 修正前服务端 `mock_server_social.c` 对 timeline 计算终点后调用
   `vm_net_mock_role_set_timeline_position(...)`，后者只更新内存 role snapshot、设
   `g_vm_net_mock_role_position_dirty=true`，不写 MySQL。
3. 修正前仅 `CBMS CLIENT_DISCONNECT (flags=0x4)` 路径会看到该 dirty 标记并调用完整
   `vm_net_mock_role_db_save("client-disconnect-position")`
   （`mock_server_transport.c:907-925`）。
4. Android remote-only transport 的正常退出会在
   `network-client.c:425-449` 发送该 flag；但 Android 强制终止没有机会执行它。
   `MainActivity.onDestroy` 最终只请求 native exit，而强制杀进程甚至不会得到
   `onDestroy` 的执行机会。
5. 服务端会话接管（`vm_mock_service_session_take_over_account`）只清理旧在线会话，
   `vm_mock_service_session_mark_offline` 不负责写 role 位置。因此它不能替代位置的
   持久化提交。

因此，**移动确认已给客户端、位置却可能仍只存在于进程内 account snapshot**。服务端
若在无显式断线的窗口内重启，或今后引入 account-state 回收，数据库仍会给重登流程
提供移动前的位置。这是一个独立于有符号 `exitID` 的持久化契约缺口。

## 修正

以 `2/1 moveinfo` 的已验证 timeline 终点作为持久化边界：在构造标准空 ACK 前，
`vm_net_mock_role_set_timeline_position(...)` 通过
`vm_net_mock_role_commit_timeline_position(...)` 只更新 active role 的
`account_roles.scene/pos_x/pos_y`。

不能调用完整 role snapshot 保存，因为后者每次移动都会删除并重建装备/背包行，既增加
延迟也扩大事务影响面。窄写入失败时会记录
`role_timeline_position_persist_failed`、还原内存 role 与 dirty 标志；不会向附近玩家传播
未提交坐标，日志标记为 `persistence=timeline-failed-rolled-back`。

## 验证

1. `make -j2` 通过；`scripts/check-service-boundary.ps1` 通过。
2. `tmp/android-force-exit-position-regression.php` 创建独立账号与角色。client A 在场景中
   上传一段 `2/1` 方向队列，收到标准 ACK 后刻意不发 `flags=0x4`；数据库立即由
   `(220,454)` 更新为 `(224,454)`，服务端记录
   `persistence=timeline-committed`。
3. 随即停止整个服务端（丢弃 account snapshot）并重新启动；新 client id B 登录同账号
   后，服务端 `session_online` 与 `scene_ready` 都使用 `(224,454)`。日志分别为
   `tmp/mock-service-19090.20260723-200729.stdout.log` 和
   `tmp/mock-service-19090.20260723-200801.stdout.log`。
4. 回归夹具已删除，最终服务端以清理后的数据库重新启动。
