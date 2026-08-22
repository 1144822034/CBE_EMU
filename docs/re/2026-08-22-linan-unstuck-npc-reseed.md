# 临安-南宣门“脱离卡死”后 NPC 目录重建

Date: 2026-08-22

## 触发与根因

实际服务日志记录角色已在 `c04临安府_01.sce`（临安-南宣门）时点击“脱离卡死”并走紧凑
`16/2` 分支：

```text
mock_settings_unstuck_16_2 ... scene=c04临安府_01.sce pos=(200,152)
WT 16/3 -> builtin-type27-followup
WT 6/1  -> builtin-scene-resource-followup
mock_scene_resource_followup_repeat_ack ... recent=1
```

该 `16/2` 响应会由 `mmGameMstarWqvga.cbm:0x11CE/0x0BCC` 调用场景重入，客户端因此销毁旧场景
壳和其中的 NPC 节点。服务端此前仅把目标标记为 completed，保留了同一精确场景的
`27/11` 目录已下发标记；后续 `WT 6/1` 于是被当作普通刷新，仅返回任务/资源对象，不再带
`npcnum`。这就是 NPC 消失的首次偏离，而不是南宣门的 NPC 数据或坐标缺失。

`27/11` 由 `scene_parse_npcinfo_and_spawn_npcs(江湖OL.CBE:0x01037998)` 消费，必须等待
`scene_runtime_init_and_sync(0x01012FB4)` 建立新场景的运行时表；因此不能把 NPC 目录塞入
先前的 `16/2` 或 `16/3` 回包。

## 修复

`vm_net_mock_build_settings_unstuck_response()` 与
`vm_net_mock_build_settings_unstuck_16_2_response()` 在确认直接场景重入后，调用新的
`vm_net_mock_mark_settings_unstuck_npc_reseed_pending()`：

- 只重新标记当前精确场景的 `27/11` 一次性目录；
- 不再发送第二个 `30/1` 或 `30/2`，避免重新打开加载流程；
- 已有的 completed-scene `WT 6/1` 分支在首个运行时请求下发非空目录；
- 后续重复 `WT 6/1` 返回空 `27/11`，不会重复创建 NPC 节点。

该标记刻意不放入通用 `vm_net_mock_mark_direct_scene_enter_completed()`：该辅助函数也会结束
首登和命名传送的路径，后两者可能已经消费过目录，通用重置会造成重复 NPC。

## 回归

扩展 `scripts/scene-transition-entry-contract-regression.c`。测试仅在进程内构造角色、场景和
WT 请求，不启动监听器、不连接 MySQL、不写入账号或客户端内存。它验证南宣门已下发过目录
后触发直接“脱离卡死”重入时：

1. 同场景目录从 seeded 重置为 pending/unseeded，场景目标保持 completed；
2. 首个 39 字节 `WT 6/1` 含唯一非空 `27/11(npcnum > 0)`，且不带场景进入对象；
3. 重复 `WT 6/1` 不再含 `npcnum`。
