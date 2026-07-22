# 传送至蓬莱铜雀台卡在场景加载

```text
phase: map-stone direct teleport -> delayed 30/1 scene enter -> same-target 2/3 completion
trigger: 从其他场景经 WT16/4、WT16/2+WT16/3 到 c00蓬莱仙岛_01.sce（蓬莱-铜雀台）
request_shape: deferred WT30/1 {scene,posinfo}; WT2/3 len=90 {2/3, 27/11, 27/4, 7/42}; then WT6/1 len=39
observed_target: c00蓬莱仙岛_01.sce @ (223,370), `download=0`
client_parser: WT30/1 -> scene_handle_enter_with_scene_pos (0x010396D6); WT30/2 -> scene_handle_change_result_scene_pos (0x01039770) -> ResetDownloadState (0x0103993C)
```

## 运行时证据与首个偏差

本次本地服务日志的实际顺序是：

```text
mock_teleport_stone_map_confirm ... scene=c00蓬莱仙岛_01.sce ... download=0
mock_teleport_stone_deferred_enter ... response=30/1
mock_scene_change_teleport_resource_pending ... response=no-30/2
net_send ... wt=6/1 len=39 source=builtin-scene-resource-followup
mock_teleport_resource_followup_complete ... completion=30/2-no-posinfo-after-WT18/7
```

这个会话中，延迟 `30/1` 和后续 `2/3` 之间没有任何实际的 `WT18/7`
资源请求。也就是说，日志把 `6/1` 解释为“最后一个资源块后的回调”，但该解释并不
适用于铜雀台这次已具备资源的进入。

更具体地说，`c00蓬莱仙岛_01.sce` 没有被
`vm_net_mock_is_current_scene_completion_request()` 的已验证场景集合识别。即使服务
端已经把这个目标标记为 pending 且当前场景名也已是该目标，`2/3` 仍落到通用
`vm_net_mock_build_scene_change_combo_response()`：该路径没有在本次 `2/3` 内给出
`27/12 -> 27/11 -> 27/4` 以及无 `posinfo` 的 `30/2` 收尾，而是继续保留 pending
状态，等到后面的 `6/1` 才补发。客户端的铜雀台场景壳因此停留在加载覆盖层。

这不是坐标、地图文件或 NPC 坐标问题：目标 `scene` 与 SCE 落点已经由
`wMap.dsh/sMap.dsh` 和 SCE 入口解析为 `(223,370)`，且本次没有资源下载。

## 修正的协议边界

仅当以下条件同时成立时，将该 `2/3` 归入现有的“当前场景完成”处理器：

1. 当前场景和请求目标都是 `c00蓬莱仙岛_01.sce`；
2. 仍存在 map-stone 直达传送的 pending 生命周期；
3. pending 目标与本次请求目标严格（宽松场景名比较）一致；
4. 请求仍是既有的 `2/3 + 27/11 + 27/4 + 7/42` 完成形状，且不是下载目标。

该处理器在同一个 `2/3` 响应中返回目标完成族，并以无 `posinfo` 的 `30/2` 最后
收尾。根据 `0x01039770` 的反编译，该对象只清理下载/加载状态，不会第二次调用
带坐标的场景进入分支。随后的 `6/1` 回到已完成场景的普通刷新路径，不再重复
`30/2`。

没有修改客户端状态、寄存器或坐标；没有伪造资源下载，也没有对其它场景放宽此
分流。

## 回归验证

- 使用隔离服务重放 `16/4 -> 16/2+16/3 -> poll/30/1 -> 2/3 completion -> 6/1`；
- 断言铜雀台的 `2/3` 响应包含一个末尾 `30/2`，并包含完整的 `27/12/27/11/27/4/7/42`
  完成对象；
- 断言后续 `6/1` 不再重复 `30/2`；
- `make -j2` 通过。
