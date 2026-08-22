# 副本怪物资源下载后远程客户端未重建场景（2026-08-22）

## 触发与证据

用户在后台部署 `测试地图.sce` 的战斗怪后，从临安 NPC 进入副本。服务端
`bin/server_out.txt` 的本次复测顺序为：

1. `mock_npc_instance_enter ... scene=测试地图.sce ... spawn_enemy=1001`
   返回一个 `30/1` 场景进入对象；
2. 客户端发送 `WT2/3 len=69`，服务端返回
   `mock_teleport_stone_current_scene_complete ... 30/2-no-posinfo` 并把目标标为完成；
3. 客户端随后才发送 `WT18/7 len=86`，服务端完整返回新版
   `测试地图.sce`，`chunk=533 total=533 crc=6440`；
4. 下载后的 `WT6/1 len=39` 命中
   `mock_scene_resource_followup_repeat_ack ... completion=none`，没有再次建立场景。

发布 overlay 的解压 payload 已验证为
`kind-8(70,38,field1=38) -> 00 00 -> kind-3(id=1001,e_batB.actor) -> EOF`，
并且服务端运行时扫描也能解析 `spawn_enemy=1001 source=SCE2-kind3`。因此本轮的
第一次偏离不是怪物字段或 SCE 记录顺序，而是新版 SCE 安装后的场景生命周期。

`bin/multiplayer-data/player-1/logs/sce-entity-callback.log` 最后修改于
2026-08-20，早于本次 2026-08-22 复测，不能作为本轮客户端已进入 kind-3 parser
分支的证据。

## 请求与客户端契约

```text
phase: instance-scene-resource-update-reentry
status: superseded by server-side completion-order fix below

request:
  wt_kind: 2 then 18 then 6
  wt_subtype: 3 then 7 then 1
  objects: WT2/3 current-scene completion includes 27/11 and 7/42;
           WT18/7 includes start/name
  key_fields: mapID=测试地图.sce, name=测试地图.sce, start=0
  sample_len: WT2/3=69, WT18/7=86, WT6/1=39
  packet_log: bin/server_out.txt (2026-08-22 15:17)

response:
  objects: 30/1; 27-family + 30/2-no-posinfo; final 18/7 chunk
  fields: scene, posinfo, totalsize=533, crc=6440, name, data

ida_evidence:
  binary: 江湖OL.CBE
  function: 0x010396D6, 0x01039770, 0x01037000, 0x01036768,
            0x01006204, 0x01018150
  dispatch_case: 30/1 scene enter; 30/2 completion; 18/7 install callback
  parser_reads: 18/7 final chunk installs the named resource before its callback
  failure_branch: without a callback-bound scene target, the remote emulator
                  cannot distinguish the required same-scene resource re-entry
                  from an ordinary duplicate entry

runtime_evidence:
  trace_lines: mock_update_chunk_complete followed by
               mock_scene_resource_followup_repeat_ack completion=none
  handled_source: builtin-update-chunk, builtin-scene-resource-followup
  queued_event: normal event 7
  client_effect: map visible, newly deployed static combat node absent

negative_evidence:
  missing_or_bad_field: none in the deployed kind-3 row
  observed_failure: CBE_CLIENT_ONLY transport discarded all scene/update
                    observations through constant false/no-op stubs
```

## 根因

桌面嵌入式传输的 `mock_server_transport.c` 已经在每个真实 event-7 回调边界维护：

- `30/1 + posinfo` 建立当前场景目标；
- `30/2` 在自己的 callback 返回后完成并清除目标；
- 最终 `WT18/7` 若文件名与刚完成场景完全相同，则只允许客户端自己的资源安装
  callback 执行一次同场景重建。

线上使用的是独立服务端加 `CBE_CLIENT_ONLY` 客户端。其 `src/network-client.c` 对
`vm_net_mock_apply_remote_observation()`、
`vm_net_mock_finish_remote_observation()` 和
`vm_net_mock_consume_update_completed_scene_reenter()` 全部使用空实现，也只给挂机战斗
包附加 observation。结果是服务端正确发送资源后，远程客户端没有保留可与该
`测试地图.sce` 安装 callback 绑定的完成目标，无法可靠地重新走原生 SCE loader。

## 修复

`src/network-client.c` 现在从实际上下行 WT 包只读提取并按 scheduler callback 顺序应用：

- 从 `WT18/7` 请求读取 `start/name`，从响应读取 `totalsize/data/name`，仅在最终 chunk
  标记安装完成；
- 从 `30/1`、`30/2` 响应读取 `scene/posinfo`，维护当前和最近完成的场景目标；
- observation 附着到原有 event-7 队列，在对应 guest callback 前应用、返回后清理；
- 只有 `updateName == completedTarget.scene` 才允许一次原生同场景重建，Actor 等附件
  下载不会触发场景重载。

该修改不改变请求或响应字节，不调用游戏业务函数，不写客户内存、寄存器或 PC/LR；
场景重建仍由客户端真实的 `WT18/7` 安装 callback 发起。

## 验证

- `make -j2`：通过；
- `scripts/remote-scene-update-reenter-regression.c`：通过；
- 回归覆盖 `30/1 -> 30/2 -> final WT18/7` 的目标恢复、一次性授权、uplink
  `start/name` 解析，以及 `e_batB.actor` 不得误触发场景重载；
- 测试不启动窗口、不连接服务端或数据库，也不操作用户进程。

实际客户端复测时应依次看到：

```text
remote_scene_target_apply ... scene=测试地图.sce
remote_scene_target_complete ...
remote_scene_target_restore ... file=测试地图.sce
remote_update_complete_apply ... action=arm-one-scene-reenter
screen_mgr allow-update-reenter ... source=remote-WT18/7
```

随后应有新的 SCE loader/entity callback，并在场景节点中出现配置的 actor ID `1001`。

## 2026-08-22 复测更正：安装 callback 不会重建场景

用户再次从临安进入 `测试地图.sce` 后，服务端确认配置和发布字节均正确：

```text
mock_npc_instance_enter ... scene=测试地图.sce ... spawn_enemy=1001 response=30/1
mock_mmgame_scene_transfer_followup ... resources+30/2-ack-no-posinfo
mock_update_chunk ... file=测试地图.sce chunk=533 total=533 crc=6440
mock_update_chunk_complete ... client-install-callback
```

客户端安装文件与服务端 overlay 的 SHA-256 均为
`C87747D0BAA5183198EB44B531E00C1A00C46469B614C02F5A6F2CF8169B9C46`。
因此配置、overlay 和 WT18/7 字节均不是本次首次偏离。

IDA 重新核对 `handle_update_chunk_response(0x010372D6)` 和
`WriteResBinToTempFile(0x01037000)` 后确认：最终资源 chunk 只调用
`LoadGameDataFromTemp("MMORPGTempbin", ...)` 和 HUD 同步；它不会调用
`EnterSceneByMapName(0x0101809C/0x01018150)`。此前“安装 callback 会主动触发同场景
重建”的假设错误，远程客户端 observation/re-entry 不是这条路径的修复点。

## 首次偏离与根因

服务端把两种不同事实混为一谈：

1. `测试地图.sce` 存在于服务端发布目录；
2. 当前客户端已在 WT18/9 清单失效后，通过最终 WT18/7 重新安装该文件及 Actor 依赖。

`vm_net_mock_prepare_scene_enter_resources()` 原先只检查
`target.needsSceneDownload`，而多个调用方又根据服务端文件存在将该标志清除。于是直接
副本 `30/1` 建立目标后，独立 `25/5` 立即收到资源族和 `30/2`，loader 被关闭；客户端
随后才请求并安装新版 SCE。安装 callback 不重建 loader，因此新版 kind-3 记录没有进入
场景节点构造，Actor 依赖也不会被请求。

此外，`mock_mmgame_scene_transfer_followup` 即使得到 `resourcesReady=false`，也会先把
资源对象和 `30/2` 写入响应，之后才记录 deferred 状态；所谓延迟完成并未延迟协议完成。

## 服务端修复

- WT18/9 按 `clientId + release id/code` 建立客户端内容状态；过期清单中的资源在各自
  最终 WT18/7 前保持 pending，断开时释放，不与其他客户端共享。
- 场景准备逻辑除 SCE 外，还按既有 SCE2 kind-3 parser 检查 field17 主 Actor 和原生
  effect Actor 尾部；不能把服务端文件存在当作客户端已经安装。
- 内容清单中的场景即使上报版本已最新，首次独立 `25/5` 也只返回同 subtype 的
  `result=4` 确认。这为缓存命中和按需缺失提供同一个真实 loader 边界。
- SCE 或 Actor 尚未完成时，后续 `6/1` 只回答请求对象，不播种场景节点、不发送
  `30/2`。最后一个依赖完成后的真实 `6/1` 才追加一次无坐标 `30/2` 并结束目标。
- `sceneCompletionSent` 明确记录已有路径是否已经发送完成对象，避免普通传送路径重复
  `30/2`。

修复没有修改 CBE/CBM、客户内存、寄存器或 PC/LR，也没有直接调用客户端场景函数。

## 回归验证

`scripts/scene-transition-entry-contract-regression.c` 现在覆盖：

```text
30/1 target
-> stale manifest scene
-> standalone 25/5: 只有 25/5 result=4，无 30/1/30/2
-> final WT18/7(SCE): 6/1 无 30/2
-> final WT18/7(main Actor): 6/1 无 30/2
-> final WT18/7(effect Actor): 6/1 恰好一个 30/2(no-posinfo)
```

同一回归还覆盖内容版本已最新但清单场景可能按需缺失的情况。另有
`content-update-manifest-regression` 验证过期版本建立 pending 集合，完全匹配版本清空
pending 集合。两项回归和 `make -j2` 均通过。

## 2026-08-22 复测更正：副本直接进入重复发送 30/1

最新崩溃复测不再走此前记录的 `WT2/3`。服务端与客户端日志对齐后的真实顺序是：

```text
26/1 NPC service -> 30/1 (resp=51)
WT2/1 len=112 -> builtin-type27-followup (resp=344)
WT6/1 len=39 -> builtin-scene-resource-followup (resp=441)
client remote_scene_target_apply serial=2
client remote_scene_target_apply serial=3
client invalid address 0x13b
```

响应 builder 的对象清单确认 `builtin-type27-followup` 只回答请求中的 `27/11`、
`27/4`、`7/42` 等对象，不含 `30/1` 或 `30/2`。第二个场景进入对象来自
`builtin-scene-resource-followup`：副本入口已经发送了带 `scene/posinfo` 的 `30/1`，但
`vm_net_mock_build_instance_enter_response()` 没有把 pending target 的
`sceneEnterPosinfoSent` 标为真。后续 `WT6/1` 因而没有进入“已有场景壳”的完成分支，而是
落入首次场景通用尾分支，再追加一个 `30/1`。客户端连续构造相同场景 screen，最终在
`0x0100DA4E` 访问 `0x13b`；该 PC 是重复生命周期的最终症状，不是修复点。

修复后的所有权为：

- NPC 副本入口发出唯一 `30/1`，同时记录该 target 的场景壳已经建立；
- 中间的复合 `WT2/1` 只回答自己请求的对象，不进入也不完成场景 target；
- 若 SCE 或 Actor 依赖未完成，`WT6/1` 只回答资源/任务对象并保持 pending，不发送
  `30/1` 或 `30/2`；
- 依赖就绪后的首个 `WT6/1` 追加且只追加一次 `30/2(no-posinfo)`，并记录
  `sceneCompletionSent`；重复 `WT6/1` 不再包含任何 `30/*` 场景对象。

`scene-transition-entry-contract-regression` 增加了上述真实顺序，并覆盖资源未就绪时的
分支。修复仍只改变服务端响应对象和 target 生命周期，不修改 CBE/CBM、客户内存、
寄存器、PC/LR，也不直接调用客户端场景函数。
