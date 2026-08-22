# 副本直入临安南宣门后进度条不关闭

## 触发与证据

从 NPC 选择副本传送，目标为 `c04临安府_01.sce`（临安-南宣门）时，服务器记录的实际序列为：

```text
WT 26/1（NPC 副本传送）
  -> WT 30/1 {scene=c04临安府_01.sce,posinfo=(162,262)}
  -> remember target pending
WT 2/3（目标场景首个确认）
  -> builtin-scene-change-current-scene-ack
  -> 27/12 + 27/11 + 27/4 + 7/42 + 30/2(no-posinfo)
WT 25/5 + 6/*（场景运行时后续请求）
  -> scene-task-subset-followup
```

该记录来自本次 `bin/server_out.txt`。`30/1` 是 NPC 副本直入已验证的场景进入契约；客户端在
`scene_handle_enter_with_scene_pos`（`JianghuOL.CBE:0x010396D6`）据此创建目标场景。随后首个
`WT 2/3` 应保留为同一 pending target 的普通无坐标确认，等待 `WT25/5 + 6/*` 完成目标场景的
资源、NPC 和任务运行时数据。

## 根因

`vm_net_mock_build_instance_enter_response()` 在返回 `30/1` 后错误设置了：

```c
g_vm_net_mock_teleport_stone_subtype3_ack_sent = true;
g_vm_net_mock_teleport_stone_direct_enter_pending = true;
```

这些标志只表示已经经历 `16/1/16/2/16/3` 的地图传送石直入。它们使
`vm_net_mock_is_current_scene_completion_request()` 把副本直入后的 `WT2/3` 误判为传送石收尾，
提前消费 pending target 并返回传送石专用的 `27/*` 组合。后续真正的场景运行时请求不再拥有
正常的完成阶段，因此客户端加载进度条会保持显示。

这不是客户端界面层的问题，也不通过强制隐藏进度条处理。

## 修复

NPC 副本直入现在明确清除全部地图传送石阶段标志，只保留正常的 pending scene target：

```text
30/1 进入目标场景
  -> WT2/3：同 target 的 30/2(no-posinfo) 普通确认，仍保留 pending
  -> WT25/5 + 6/*：目标场景资源/NPC/任务数据，完成 pending target
```

该路径仍完全由客户端的 `30/1`、`30/2` 和后续场景运行时 parser 驱动，未修改客户端内存、
场景状态或界面回调。

## 回归

`scripts/instance-guide-direct-entry-regression.c` 除了验证副本直入返回单个 `WT 1/30/1` 外，
还预置并断言直入构造器会清除三个地图传送石阶段标志，防止未来再次将 NPC 副本直入路由到
传送石 completion handler。
