# player-1 NPC 副本进入后场景加载停滞

Date: 2026-08-30

Status: validated (isolated regression)

## 1. 当前卡点

- 可见现象：player-1 通过 NPC 的“副本传送”进入 `29梦境空间_03.sce` 后停在场景加载界面，无法自然退出。
- 触发方式：在 `c04…_06.sce` 与 actor `30406` 对话并选择副本传送。
- 本轮最小目标：确定 NPC 的 `30/1` 入口之后，首次 `WT2/3`、`WT25/5`、`WT18/7` 中哪一项没有完成客户端场景状态机，且只修复该协议契约。

## 2. 运行时证据

- `bin/server_out.txt` 当前运行记录：`builtin-npc-service` 对 actor `30406` 返回 `30/1 {scene=29梦境空间_03.sce,pos=(120,120)}`；随后收到 `WT2/3 len=89`、`WT25/5 len=9` 和 `WT18/7`。
- 首个可疑响应是 `WT2/3` 被标为 `mock_scene_change_pending_repeat_ack ... posinfo_sent=1 action=ack-only-keep-phase`，即没有再次提供位置型场景进入对象。
- 随后 `WT25/5` 返回 `resources+30/2-ack-no-posinfo`，资源请求 `WT18/7` 则确实返回了 `29梦境空间_03.sce` 的完整 chunk；资源安装后又出现一个 `WT25/5`，却仅由 `builtin-scene-default-event` 以 23 字节确认。
- 当前日志尚未出现资源安装后的正常 `WT6/1` 完成请求，因而不能把 `WT18/7` 的成功响应直接当作场景完成证据。

## 3. 已有客户端证据

| binary | function/address | reason | findings |
| --- | --- | --- | --- |
| `江湖OL.CBE` | `scene_handle_enter_with_scene_pos` / `0x010396D6` | `30/1` 首次场景进入 | 读取 `scene` 与 tagged `posinfo`，再推进场景进入。 |
| `江湖OL.CBE` | `scene_handle_change_result_scene_pos` / `0x01039890` | `30/2` 场景 change 结果 | 只有位置型首次结果才建立目标场景；收尾结果不能重复位置型进入。 |
| `江湖OL.CBE` | `ProcessSceneState` / `0x01003CFC` | loading 状态 | `state == 100` 显示 loading，`state == 101` 进入场景回调。 |

以上地址来自已有场景进入调查记录；本环境当前没有可调用的 IDA MCP，因此实现前将用现有静态证据、最新运行包序列和生产 handler 的状态所有权交叉核对。

## 4. 当前假设与排除项

- 假设：NPC 入口将目标标记为已发送 `posinfo` 后，后续的首次 `WT2/3` 被误分类为重复确认；资源下载完成后没有接到与该阶段匹配的场景完成响应，loading 因而没有自然收尾。
- 已排除：不是目标 SCE 缺失。`WT18/7` 返回 `29梦境空间_03.sce` 的完整 278 字节 chunk，并记录了客户端安装 callback。
- 已排除：不能通过伪造任意资源完成、强制切换 screen、改客户端内存或重投回调处理；这些都不会证明 `30/1/30/2` 场景契约已完成。

## 5. 调用链和可实现根因

1. `vm_net_mock_build_instance_enter_response()` 返回唯一的 `30/1 {scene,posinfo}`，并将相同 target 记录为 `sceneEnterPosinfoSent=true`、`sceneCompletionSent=false`。
2. player-1 的下一条 `WT2/3` 与该 pending target 完全相同；它是 direct-enter 场景壳的跟随请求，不是普通门户的首次位置型 `30/2`。
3. `vm_net_mock_build_scene_change_combo_response()` 现有代码在识别 `samePendingTarget` 前就无条件附加 `30/2(no-posinfo)` 并写入本地 `target.sceneCompletionSent=true`。这违反了“资源边界完成才发送一次 no-posinfo `30/2`”的既有契约。
4. `vm_net_mock_build_mmgame_scene_transfer_followup_response()` 因此跳过 `needsContentLoadBoundary`，把首个 `WT25/5` 当作可结束的资源完成。SCE 安装后的第二个 `WT25/5` 已无 pending target，只能落入 `builtin-scene-default-event`。

### 本轮实现

- 在既有、已由 `WT2/3` 签名限定的 `vm_net_mock_build_scene_change_combo_response()` 中，识别“唯一 `30/1` 已建立场景壳但尚未完成”的同目标跟随请求。
- 该分支继续回答本请求已有的 `7/42`、`27/*` 等对象，但**不发送** `30/2`，也不设置 `sceneCompletionSent`。
- 保持 target pending，令第一个 `WT25/5` 经过原有 `needsContentLoadBoundary` 等待实际 `WT18/7`；安装后的下一个同类 `WT25/5` 仍由现有 mmGame builder 附加唯一的 `30/2(no-posinfo)`。
- 不新增宽泛 detector、不改客户端/宿主状态，不为资源请求伪造完成。

实现位置为 `src/server/mock_server_social.c` 的
`vm_net_mock_build_scene_change_combo_response()`：只有同一 pending target 已由
direct NPC `30/1` 建壳、且尚未发送完成确认时，才返回请求内原有的服务对象而保留
资源边界；其他 `WT2/3` 路径不变。该分支会记录
`mock_scene_change_direct_enter_followup_ack`，方便把本轮实际 player-1 请求与修复
分支对应起来。

## 6. 回归计划

- 在 `scene-transition-entry-contract-regression` 增加真实顺序：`30/1 -> 同目标 WT2/3 -> WT25/5(loader boundary) -> WT18/7 -> WT25/5(30/2 no-posinfo)`。
- 断言第一条 `WT2/3` 不含任何 `30/*`，pending target 仍有效且 `sceneCompletionSent=false`；首次 `WT25/5` 不含 `30/2`，资源安装后的第二次才恰含一个无坐标 `30/2`。
- 执行 `make -j2` 和隔离回归；不启动 listener、不连接或写入 player-1 数据。

## 7. 验证结果

- `make -j2`：通过；生产 client、server 与 `bin/jh-online-server.exe` 均完成链接。
- 隔离的 `scene-transition-entry-contract-regression`：通过。夹具使用当前 player-1
  的“manifest 已登记但目标 SCE 首次缺失”状态，并验证完整序列：
  `30/1 -> WT2/3 -> WT25/5 -> WT18/7 -> WT25/5`。
- 该回归确认：同目标 `WT2/3` 不含 `30/1` 或 `30/2`，首个 `WT25/5` 只保留下载边界，
  SCE 安装后的第二个 `WT25/5` 恰包含一个无坐标 `30/2`，随后 target 完成；未启动
  listener、未连接 MySQL、未写入 player-1 数据。

人工复测时应依次看到 `mock_scene_change_direct_enter_followup_ack`、下载边界、
`mock_update_chunk_complete`，以及安装后的 `mock_mmgame_scene_transfer_followup ...
30/2-ack-no-posinfo`；若最后一步仍落入 `builtin-scene-default-event`，应保留该次完整
请求/响应日志继续取证。

## 8. Unknowns

- 该 player-1 运行的 `WT2/3` 仍需在下一次人工复测中保留原始对象清单；当前代码修复只依赖已验证的同 target、已发送 `30/1` 和未完成标记，不把未读字段解释为业务语义。
- 本环境没有可调用的 IDA MCP；本轮引用既有 `0x010396D6`、`0x01039770` parser 记录，运行时响应对象顺序与生产 builder 相互验证。
