# 场景往返后 NPC 目录缺失

Date: 2026-07-24

Status: validated

## 1. 当前卡点

- 可见现象：角色从一个场景切换至另一个场景后，再回到原场景，NPC 有概率不显示。
- 触发方式：进入有 NPC 目录的场景 A，切换到 B，再回到 A；本轮先以精确资源键
  `c00蓬莱仙岛_03.sce`、`01桃花岛_01.sce` 和 `00蓬莱仙岛_02.sce` 做隔离复现。
- 本轮最小目标：确认新场景壳发出的首次 `1/27/11` 是否收到非空且属于原始场景键的
  `npcnum/npcinfo`，并仅在该壳边界修复一次性目录状态。

## 2. 运行时/资源证据

- `docs/re/2026-07-20-dynamic-npc-scene-roundtrip.md` 已记录本类问题的有效包族：
  `2/3` 场景变更、随后 `25/5` 资源/任务子集，或 `30/2` 收尾的本地场景壳完成。
- 最近的场景 key 审计已确认：仅可把同一 key 的可选 `.sce` 扩展名视作相同；不得将
  `c00蓬莱仙岛_02.sce`、`00_蓬莱仙岛02.sce` 或 `00蓬莱仙岛_02.sce` 互相替换。
- 当前服务端用 `sceneMoveinfoNpcPending(Scene)` / `sceneMoveinfoNpcSeeded(Scene)` 维护
  `27/11` 的一次性发送状态，并由账号会话捕获/恢复。这是本轮要验证的状态边界。
- 隔离服务 `19095` 的 `00蓬莱仙岛_02.sce -> c00蓬莱仙岛_01 ->
  00蓬莱仙岛_02.sce` 回归通过：返回 `2/3` 中有非空 `27/11`，说明普通场景往返、
  exact-key 目录和该一次性状态并非本次首次偏离。
- 同一隔离会话复现传送石序列 `16/4 -> 16/2+16/3 -> poll(30/1) -> 2/3 -> 6/1`。
  测试夹具只在精确目标 `01桃花岛_02.sce` 临时加入 actor `59422`，服务日志确认
  `scene_npc_request_snapshot ... selected=1 total=1 dynamic=1`，但最终 `6/1` 的 237 字节
  响应不含任何 `27/11`，回归以 `npcnum=NULL` 失败。

## 3. IDA 证据

| binary | function/address | findings |
| --- | --- | --- |
| `江湖OL.CBE` | `scene_runtime_init_and_sync` `0x01012FB4` | 新场景运行时会重建节点表并发出后续场景同步请求；同场景名不代表仍是旧场景壳。 |
| `江湖OL.CBE` | `scene_parse_npcinfo_and_spawn_npcs` `0x01037998` | 仅当响应有非零 `npcnum` 与 `npcinfo` 时，为每一行创建 type-21 NPC 节点；空 `27/11` 不会创建 NPC。 |
| `江湖OL.CBE` | `scene_handle_change_result_scene_pos` `0x01039770` | `30/2` 是独立的场景完成路径，不会隐式重放 NPC 目录。 |

## 4. 当前调用链

1. 客户端完成 `2/3`/`25/5` 或 `30/2` 场景转换，建立一个新的场景壳。
2. 服务端的完成 builder 以目标 scene key 调用
   `vm_net_mock_mark_scene_moveinfo_npc_seed_pending()`。
3. 第一个适用的场景资源/任务响应调用
   `vm_net_mock_append_scene_npcs11_once_or_empty()`。
4. 客户端 `0x01037998` 消费非空 `27/11` 并创建 NPC 节点；随后任务提示刷新才能附着在节点上。

## 5. 首个错误状态与根因

`vm_net_mock_build_scene_resource_followup_response()` 的
`completeTeleportResourceEnter` 分支明确识别了“延迟传送完成后的首个 WT6/1”。它在
`30/2(no-posinfo)` 前调用：

```c
vm_net_mock_append_scene_npc_lifecycle_seed(..., target.scene, false, true)
```

第四个参数 `allowStartupSeed=false` 禁用了与
`vm_net_mock_mark_scene_moveinfo_npc_seed_pending(target.scene)` 配对的 pending seed；第五个
参数只允许商城返回 seed。于是此分支即使拥有目标场景的一次性 pending 目录，也永远不会
调用 `vm_net_mock_append_scene_npcs11_once_or_empty()`，并在同一响应末尾把场景转换标为完成。
这正是新场景壳第一次缺少 NPC 创建数据的位置。

这不是目录为空、`c` 前缀/下划线别名、坐标或轮询时序问题：临时 NPC 已由同一精确 key
选择出来；普通往返路径也已成功。客户端 `0x01012FB4` 在最终 `6/1` 后工作，且
`0x01037998` 需要这一次非空 `27/11`。

## 6. 负面约束

- 不以 scene-sync poll 补发目录；该 poll 晚于场景初始化且会隐藏正确完成点。
- 不把临时 `01桃花岛_02.sce` NPC 或任何 `00蓬莱仙岛_02.sce` 目录复制给其他 key。
- 不改变 `30/2` 顺序，也不在后续普通刷新重复目录。一次性状态仍由既有
  `pending/seeded` 契约消费。

## 7. 本轮实现计划

1. 将最终传送资源完成分支的 `allowStartupSeed` 置为 true，使其消费该 target 的已有
   pending seed；商城返回权限保持 true。
2. 保持 `27/11` 在资源/任务对象之前、`30/2(no-posinfo)` 之前的现有客户端顺序。
3. 用同一隔离回归同时验证普通 A→B→A 和传送资源完成两条路径；后者应只多出一次
   非空 `27/11`。

## 8. 验证清单

- [x] A→B→A 的首次新场景壳响应含非空 `27/11`。
- [x] 传送资源完成路径的目录 key 是目标 `01桃花岛_02.sce` 的精确 key。
- [x] 最终 `WT6/1` 在 `30/2` 前含一次非空 `27/11`；修复后夹具临时 actor 的
  `npcnum=1`、响应长度 `336`，此前为缺少 `27/11` 的 `237`。
- [x] 普通 A→B→A 的返回仍为一次 `npcnum=3` 目录，未引入后续重复 follow-up。
- [x] 测试夹具仅在隔离服务启动期间添加目标场景的动态 NPC，并在退出后删除。
- [x] `make -j2` 通过；主服务已重启并监听 `19090/19091`。

## 9. 修改结果

`mock_server_interaction_login.c` 的资源完成分支现在将
`allowStartupSeed=true` 传入既有 `vm_net_mock_append_scene_npc_lifecycle_seed()`。
这不是新的兜底或补发机制：它只消费前一条 `30/1` 已对**同一精确 target key** arm 的
pending seed，仍由 `seeded` 标记阻止普通刷新再次下发目录。`27/11` 仍位于
资源/任务对象和末尾 `30/2(no-posinfo)` 之前，符合 `0x01037998` 创建节点、
`0x01039770` 收尾下载的客户端顺序。

## 10. 用户复测反证：普通回程的首次偏离仍未修复

用户在主服务复测了实际路径 `00蓬莱仙岛_02.sce -> c00蓬莱仙岛_01.sce ->
00蓬莱仙岛_02.sce`，返回后 NPC 仍不可见。主服务日志中的同一会话
`guest00023/4a8fcfe5` 已保留完整证据：

1. 进入 `c00蓬莱仙岛_01.sce` 时，`25/5` 的
   `mmgame-scene-transfer-followup` 确实下发 `npcnum=3`，且客户端可继续交互。
2. 返回 `00蓬莱仙岛_02.sce` 时，`WT2/3` 的
   `scene-change-full-bootstrap` 也构造了精确 key 的 `npcnum=3/npcinfo_len=214`，
   随后才下发 `scene-change-post-enter-followup`、任务刷新和移动包。
3. 因而本次实际失败不是目录选择为空，也不是资源下载 `WT6/1` 路径漏 seed；首个已知
   错误状态是：**新场景壳尚未完成时，在 `WT2/3` 复合响应内消费了一次性 `27/11`，服务端
   已将该目录标为 seeded，但客户端最终没有保留对应 NPC 节点。**

此前隔离回归只断言原始响应中存在 `27/11`，不能证明客户端已经进入接受该对象的场景
parser/UI 生命周期；它不能作为“普通回程已修复”的证据。不得以轮询补发或重复目录掩盖
这个偏离。

下一步只调查 `WT2/3` 内的对象顺序、客户端处理 `30/2` 建壳的时点，以及该场景随后真实
发起的首个可接收 `27/11` 请求；随后将一次性目录移动到这个经客户端验证的边界，并让
`pending/seeded` 的消费与该边界保持一致。

## 11. 普通回程修正与验证

`vm_net_mock_build_scene_change_combo_response()` 的 full-bootstrap 分支现在只对目标精确
scene key 调用 `vm_net_mock_mark_scene_moveinfo_npc_seed_pending()`；它不再把 `27/11` 追加到
`WT2/3` 的 `30/2(posinfo)` 之前，也不消费 `seeded` 标记。该响应仍负责唯一的带坐标场景
完成，仍包含原有资源/任务对象，未引入第二个 `30/1` 或 `30/2`。

客户端随后实际发出的
`WT25/5 { 25/5, 2/3(maptype,mapID,exitID), 27/11, 7/42 }` 由既有
`scene-change-post-enter-followup` builder 处理。此时目标已是 recent-completed scene；该
builder 先回无坐标 `30/2` 用于完成 UI 清理，再从仍 pending 的**同一精确 key** 消费一次
非空 `27/11`。这与已记录的 `c00蓬莱仙岛_01.sce` 延迟传送修正使用相同的客户端请求边界，
但没有把该规则扩展为轮询重放。

重新启动隔离服务后，`scripts/scene-npc-return-regression.php` 使用真实复现路线
`00蓬莱仙岛_02.sce -> c00蓬莱仙岛_01.sce -> 00蓬莱仙岛_02.sce` 验证：

- 返回 `WT2/3` 为 439 字节，未含非空 `27/11`；
- 紧随的 79 字节 post-enter 请求返回 353 字节，日志为
  `mock_scene_npc_seed phase=post-enter-repeat ... npcnum=3`；
- 资源下载传送的独立路径仍在最终 `WT6/1` 返回 `npcnum=1`，未被普通回程改动影响；
- `make -j2`、`git diff --check` 通过；测试账户与临时动态 NPC 均已清理。
