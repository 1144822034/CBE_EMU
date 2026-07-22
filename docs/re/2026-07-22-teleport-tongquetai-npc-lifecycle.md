# 瞬移至蓬莱铜雀台后 NPC 不显示

```text
phase: direct map-stone scene completion -> scene runtime resource follow-up
trigger: 从其他场景瞬移至 c00蓬莱仙岛_01.sce（蓬莱-铜雀台）
request_shape: delayed WT30/1; WT2/3 len=90 {2/3,27/11,27/4,7/42}; WT6/1 len=39
client_parser: WT27/11 -> scene_parse_npcinfo_and_spawn_npcs (JianghuOL.CBE:0x01037998)
```

## 运行时证据与根因

铜雀台加载停滞修正后，服务端最新复现记录为：

```text
mock_teleport_stone_deferred_enter ... scene=c00蓬莱仙岛_01.sce
mock_scene_npc_seed phase=current-scene-completion ... npcnum=4 ... once=1
mock_teleport_stone_current_scene_complete ... response=27-family+30/2-no-posinfo
net_send wt=2/3 len=90 source=builtin-scene-change-current-scene-ack
mock_scene_resource_followup_repeat_ack ... scene=c00蓬莱仙岛_01 objects=5 completion=none
```

服务端在 `2/3` 响应内已经发送了四个 NPC 的非空 `27/11`，并把
`g_vm_net_mock_scene_moveinfo_npc_seeded` 置为真。随后客户端正常发出 `6/1`，但
该请求被当作已完成场景的普通刷新处理，没有任何 NPC 对象；场景轮询也持续记录
`npc=0`。客户端实际表现正是“地图已显示、NPC 全部缺失”。

首个被违反的生命周期契约是：本次 `2/3` 的职责是关闭延迟 `30/1` 留下的加载状态，
而非消耗场景运行期的 NPC 创建目录。`0x01037998` 的非空 `27/11` 应在客户端发出
后续 `6/1`、已进入 scene-runtime 初始化阶段时发送。提前发送后，服务端的一次性
目录状态阻止了正确阶段的重放。

## 修正

仅针对下列条件：map-stone 直达、pending 目标匹配且目标是
`c00蓬莱仙岛_01.sce`：

1. `2/3` 仍返回空 `27/11`、`27/12/27/4/7/42` 与末尾无 `posinfo` 的 `30/2`，
   因而保留已修复的加载完成契约；
2. 不消费 NPC 一次性目录，保持它与铜雀台场景关联；
3. 客户端真实发送的紧随 `6/1` 响应先返回非空 `27/11`，再返回普通资源/任务刷新；
4. 后续刷新不会再次下发 `30/2` 或 NPC 目录。

没有改变客户端状态、没有重复广播 NPC，也没有对其他场景放宽分流。

## 回归

- 隔离服务重放 `16/4 -> 16/2+16/3 -> poll/30/1 -> 2/3 -> 6/1`；
- 断言 `2/3` 的 `27/11` 是空门控对象，仍带唯一 `30/2`；
- 断言紧随 `6/1` 的 `27/11` 为非空 NPC 目录，且不带第二个 `30/2`；
- `make -j2` 通过。
