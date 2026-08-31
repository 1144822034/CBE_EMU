# 2026-08-28 远端角色行走跳帧：下行 catch-up 截断违反整段回放契约

phase: scene-sync downlink movement timeline
status: fixed

## 触发与现象

双客户端联机（`bin/multiplayer/start-all-4.bat`）中，一个客户端连续行走时，
另一个客户端上的远端角色每秒出现一次"瞬移 + 小步快走"：前缀坐标直接跳进
外层 x/y，只剩末尾几步以动画播完。用户描述为"远端下发数据不正常，像跳帧"。

## 协议链路（已在既有取证中验证）

1. 源客户端 `scene_runtime_tick (0x01014EE0)` 每 100ms 采一帧方向，攒满十帧后
   上传 `WT 2/1 moveinfo`（字节 `1..4` 为方向，`0` 为空闲帧）。
2. 服务端预算限速（`CBE_*` 见 2026-07-23 文档）接受方向前缀后，整段已接受
   timeline 连同真实起点写入 `pendingDirQueue{Blob,Len,StartX,StartY,EndX,EndY,
   Serial}`，每源每次上传 serial 自增。
3. 观察者客户端每 100ms 场景同步轮询（`CBMS` bit1）；对每个新 serial，服务端
   下发一个 `1/2/2 { startX, startY, actorId, len16, raw }` 增量对象。
4. 观察者 `net_handle_actor_move_info case 2 (0x01012ADC)` →
   `scene_node_update_move_blob (0x01012A76)`：外层 x/y 写入 `node+24/+26`，
   raw blob 拷入 `node+136`，重置读游标；
   `ProcessSceneAutoAction (0x01045428)` 每渲染帧（100ms）消费一字节、每方向
   步进 4 像素。

## 根因（首个偏离位置）

`mock_server_social.c` 的 `vm_net_mock_build_scene_player_moveinfo_blob` 在
2026-07-10 文档 "Latency Reduction" 中加入的下行 catch-up：按
`CBE_SCENE_SYNC_MAX_CATCHUP_STEPS`（默认 4）只保留末尾 4 个非零方向字节，把
被跳过的前缀逐字节折算进外层 x/y。

连续行走时每秒一批 10 帧约含 10 个方向步：前 6 步（24 像素）被瞬移进外层
坐标，仅 4 步（16 像素）以动画回放。观察者每秒看到一次 24 像素瞬移，且远端
动画被压缩到 400ms 内播完。该改动违反 IDA 验证的客户端契约——外层 x/y 是
时间线起点，客户端从起点起逐帧回放整个 raw blob（含 `0` 空闲帧，空闲帧承载
节奏）。"回放落后源最多约 1 秒"是客户端生产端攒批的固有下界，不能用篡改
外层坐标的方式消除。

## 修改点

- `src/server/mock_server_social.c`：删除前缀瞬移/尾部截断逻辑，恢复按契约
  下发 `pendingDirQueueStartX/Y + 完整 pendingDirQueueBlob`；verbose 日志改为
  `scene_move_delta`（含 serial、frames、start、end）。
- 删除 `CBE_SCENE_SYNC_MAX_CATCHUP_STEPS` 旋钮（无其余引用）。
- 上行限速、每观察者投递游标（`peerSync->lastMoveSerial`）、100ms 轮询等
  其余延迟优化保持不变。

## 验证

- `make -j2` 双目标构建通过。
- 观察者侧预期：连续行走时每个 serial 收到一次 `len=10` 的完整 timeline，
  客户端日志 `scene_actor_update_move ... len=10`，远端角色以原始 100ms 节奏
  连续行走，仅整体滞后源约 1 秒，无瞬移。
- 待用户双客户端手测确认主观平滑度。

## 已知边界与风险

- 远端角色位置滞后源约 1 秒（生产端十帧攒批所致），属协议固有延迟；若后续
  需要降低该延迟，正确层级是研究真实服务端的分批下发节奏，而不是再改外层
  坐标语义。
- 上行位置型 moveinfo entry（非 timeline）契约仍未解（见 2026-07-10 文档
  Unknowns），本修复不涉及。
