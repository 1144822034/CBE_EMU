# 跨场景周围玩家可见性复核（2026-07-23）

## 状态

调查中。用户报告：两个客户端已经处于不同场景时，客户端仍会在当前地图上看到对方
移动。该现象必须由服务端下行的附近玩家对象触发，不能用客户端隐藏或丢弃响应处理。

## 已确定的协议契约

- `1/2/10 otherinfo` 创建或刷新周围玩家节点；`1/2/2 moveinfo` 只更新既有节点的
  移动队列；`1/2/6 actorid` 删除节点。
- 观察端的 scene-sync poll 应仅从与观察端**同一已 ready 场景**的在线会话生成
  `1/2/10` / `1/2/2`，已不满足条件的已知对象必须在下一轮 poll 中接收 `1/2/6`。
- `30/1 scene + posinfo` 是客户端场景进入，不是原场景内坐标更新。发送该对象后，源
  会话应先进入 `sceneVisiblePending`；完成目标场景的客户端驱动 follow-up 后才能进入
  `sceneVisibleReady` 并作为目标场景附近玩家出现。

相关客户端解析证据和既有特殊场景交互缺失 pending 的根因，见
[`2026-07-22-cross-scene-nearby-isolation.md`](2026-07-22-cross-scene-nearby-isolation.md)。

## 当前业务链路与检查点

```text
切图请求 / 场景响应 (30/1 或 30/2 result=1)
  -> session scene_pending（立即从旧场景种子移除）
  -> 旧观察端 poll: 1/2/6
  -> 客户端进入目标场景并请求 resource/task follow-up
  -> session scene_ready（目标场景、持久化角色位置）
  -> 目标观察端 poll: 1/2/10，然后才可 1/2/2
```

隔离服务（`127.0.0.1:19093`）上的真实 TCP 回归先证明：已有的特殊场景交互
`WT 4/1 -> 30/1` 路径本身仍正确执行源场景 `1/2/6`、pending 期间不出现在目标场景，
目标 scene follow-up 后才产生 `1/2/10`。

随后在 A 收到 `30/1` 与 A 发送目标场景 startup follow-up 之间，重放客户端正常可能
残留的十帧 `WT 2/1 { moveinfo }`。第一次偏离被固定为：

```text
scene_pending client=7a220101 ... target=01桃花岛_02.sce
WT 2/1 moveinfo (late ten-frame timeline)
-> scene_ready client=7a220101 ... reason=moveinfo-upload       # 错误
-> observer C in 01桃花岛_02 poll: 1/2/10 + 1/2/2 for A         # 错误后果
```

`vm_net_mock_build_actor_moveinfo_ack_response()` 先从已持久化的目标角色位置取得 scene，
再调用 `vm_mock_service_session_update_move_position()`；后者此前会在
`sceneVisiblePending` 时调用 `mark_scene_ready()`。但是 `1/2/1` 只是移动上传，既不表示
客户端完成 `30/1` 场景进入，也不是 target resource/task follow-up 的替代信号。它违反了
上面的生命周期契约，导致尚在旧场景运行的玩家被目标场景观察者创建并播放移动队列。

修复点：在 `1/2/1` handler 内，若活动会话仍为 `sceneVisiblePending`，返回客户端原本
要求的空 `1/2/1` ACK，但不得保存该队列、改写角色位置或提升 ready。唯一允许 pending
转 ready 的路径仍是目标场景完成时已有的 follow-up/完成响应。

同时将 `vm_mock_service_session_update_move_position()` 收紧为纯位置更新：它不再把
not-ready、pending 或场景不匹配的移动上传提升为 ready。这样即使后续调用点遗漏预检，
生命周期的拥有者也不会再把移动语义扩大为场景进入语义。

## 已排除的路线

- 不把另一场景玩家的坐标改写为当前场景坐标；
- 不在客户端补丁或渲染层强制隐藏 actor；
- 不通过静默丢弃 `1/2/10`、`1/2/2` 或重复清空节点掩盖错误。

## 回归

`tmp/scene-nearby-isolation-regression.php` 已按当前 `otherinfo` 顺序行格式解析 actor id，
并覆盖「`30/1` 后、目标 follow-up 前抵达一个十帧 `1/2/1 moveinfo`」：目标观察端在该阶段
不得收到源角色，完成 follow-up 后才可收到角色。修复前该新增断言稳定失败，并由上述服务
日志记录 `reason=moveinfo-upload` 与目标场景 baseline 佐证。

修复后在新建的隔离服务中通过：

```text
scene_lifecycle_moveinfo_ack ... ready=0 pending=1
  action=ack-without-ready-or-position
observer C target poll: movement=0                 # A 仍不可见
scene_ready A ... reason=scene-task-subset-followup
observer C target poll: roles=1                    # A 此时才出现
new post-ready 1/2/1 -> observer C: movement=1     # 正常移动仍同步
scene nearby isolation regression passed removal=2/6 target-baseline=2/10
```

`make -j2` 和 `make boundary-check` 均通过。
