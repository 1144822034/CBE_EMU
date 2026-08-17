# 首次传送后场景 NPC 未创建

Date: 2026-08-17

## 触发与首次偏离

复现顺序是登录到 `c04临安府_05.sce`、第一次通过边缘传送进入
`c04临安府_01.sce`，再继续一次传送后 NPC 才出现。

首次传送日志显示：

1. `WT 2/3` 返回一次带坐标的 `30/2`，并保留目标等待场景 follow-up。
2. 紧随的 78 字节 post-enter 请求被 `builtin-scene-change-post-enter-followup` 处理。
3. 该回包原先立即包含非空 `27/11(npcnum=3)`，随后第一个 `WT 6/1` 只走普通
   `mock_scene_resource_followup_repeat_ack`。

`scene_parse_npcinfo_and_spawn_npcs(0x01037998)` 要求
`scene_runtime_init_and_sync(0x01012FB4)` 已建立运行时表。上述非空目录在 post-enter
阶段过早消费了唯一的一次性标记，因此真正可创建节点的 `WT 6/1` 不再获得目录。

## 修复

`vm_net_mock_build_scene_change_post_enter_followup_response()` 现在仅返回空 `27/11` gate，
保留同精确场景的 pending、unseeded 目录。后续真实 `WT 6/1` 继续由既有
`deferredTeleportNpcSeedAfterCurrentCompletion` 分支下发一次非空 `27/11`。

该修改仅影响普通场景切换的 post-enter builder；商城返回和地图石的独立完成路径不改变。

## 协议回归

`scripts/scene-transition-entry-contract-regression.c` 现以真实的 78 字节
post-enter 请求和 39 字节 `WT 6/1` resource/task 请求复放该边界。它不启动
监听器、不连接 MySQL，也不修改账号或客户端内存。2026-08-17 已通过，断言：

1. post-enter 响应包含请求所需的空 `27/11`，没有 `npcnum`，并保留同场景
   pending、unseeded 目录；
2. 首个 `WT 6/1` 包含唯一非空 `27/11`，`npcnum > 0`，且不追加 `30/1` 或
   `30/2` 场景进入对象；
3. 重复 `WT 6/1` 不再带 `27/11.npcnum`，避免重复创建场景节点。

## 真实客户端验证边界

构建后，首次传送的服务端日志应依次包含：

```text
mock_scene_npc_seed_defer ... phase=scene-change-post-enter ... next=WT6/1
mock_scene_npc_seed_deliver ... phase=WT6/1 after=completed-scene-runtime-init
mock_scene_npc_seed ... phase=completed-scene-runtime-followup ... npcnum=<nonzero>
```

其中第二行只能出现一次。客户端画面应在第一次传送后立即显示目的场景 NPC。
