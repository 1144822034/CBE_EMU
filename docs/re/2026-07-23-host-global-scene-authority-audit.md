# 2026-07-23 宿主全局场景与远端会话权威来源审计

## 范围与结论

本记录审计 Windows `bin/main.exe --mock-service-only` 对远端请求的场景、坐标、
场景切换和战斗目标来源。结论是：`Global_R9` 并不是网络客户端的全局状态；它是
**承载模拟服务的本地 CBE 模拟器进程**的主模块基址。`Global_R9 + 0x54AC` 指向该
本地 CBE 当前场景对象，其 `+0x475` 是场景名；`Global_R9 + 0x5C64 + 0x40` 指向
该本地 CBE 当前玩家场景节点，其 `+0x18/+0x1A` 是格点坐标。

远端请求本身没有共享这个 CBE 内存。因此本地模拟器停在蓬莱时，不能用它回答
一个位于桃花岛的远端账号。远端服务的正确权威顺序应为：

```text
经协议确认的本请求字段
    -> 当前 client session 的可见状态
    -> 当前已选择 role 的 MySQL 持久化状态
    -> SCE/DSH 等服务端资源的确定性解析
    -> unresolved（记录并继续取证）
```

宿主 `Global_R9`、`CBE_SCENE_KEY`、`CBE_SCENE_POS_X/Y` 只能是未绑定角色时的本地
诊断输入，不能覆盖在线角色。

Windows `Makefile` 仅定义 `-DNETWORK_SUPPORT`，没有定义 `CBE_SERVER_ONLY`；所以
下列 `#ifndef CBE_SERVER_ONLY` 分支在当前 Windows 服务进程中可以执行。Linux 的
`bin/jh-online-server` 会定义 `CBE_SERVER_ONLY`，其中部分 grid/pending 分支已被
编译为 `false`，但这不是 Windows 服务的安全保证。

## 已验证的原问题

phase: settings-unstuck-current-scene

status: validated

request:

```text
WT 0/12/3 { id: typed-u32 }
```

response:

```text
12/3 { result: typed-u8 1, id: typed-u16 }
16/3 { result: typed-u8 2, scene, posinfo, exitid }
```

IDA evidence:

- `mmGameMstarWqvga.cbm:0x5BCA` 构造设置请求；
- `sub_11CE(0x11CE)` 仅在 `16/3 result=2` 时进入场景；
- `sub_BCC(0x0BCC)` 读取 `scene,posinfo,exitid`。

旧的 `vm_net_mock_current_scene_name()` 先读取宿主 `Global_R9`，随后脱离 builder
立即把该 `scene` 保存到当前 role。现已改为 role 优先，并在
`vm_net_mock_get_current_scene_unstuck_target()` 中显式选择 role/session 坐标。

runtime evidence:

- 回归进程故意设置宿主 `CBE_SCENE_KEY=c00蓬莱仙岛_01.sce`；
- 角色持久化场景为 `01桃花岛_02.sce`；
- 输出：`mock_unstuck_target ... scene_source=role-db ... pos=(160,342)`；
- `16/3.scene` 与 `account_roles.scene` 都保持 `01桃花岛_02.sce`。

详情见 [2026-07-23-unstuck-scene-authority.md](2026-07-23-unstuck-scene-authority.md)。

## 同类来源清单

下表按“宿主状态能否改变远端可见结果或持久化数据”分级。`已确认`表示从调用链可
证明会把宿主值进入响应/持久化；不表示已经在用户现场复现。

| 优先级 | 位置 | 宿主来源与影响 | 当前状态 / 后续取证 |
| --- | --- | --- | --- |
| P0 | `mock_server_equipment_npc.c:1330` `vm_net_mock_save_player_pos_state` | 当调用者传入非法/空 scene 时，先用 `vm_net_mock_read_runtime_scene_name()`，后用 role scene；任一调用会把宿主场景写入当前角色。 | 已确认风险。当前唯一直接调用者是战斗组合包的 snapshot；应先让 role/session 优先，不能以默认出生场景代替未知 scene。 |
| P0 | `mock_server_social.c:4782` → `mock_server_battle.c:5625` | `vm_net_mock_snapshot_current_player_pos()` 从宿主当前玩家 node 读坐标，传 `scene=NULL` 给上述保存函数；触怪/战斗请求带 `moveinfo` 时可覆盖远端角色的场景和坐标。 | 已确认调用链；需保存该战斗请求、响应与两场景双客户端日志后，改为请求 moveinfo 或 active session/role。 |
| P0 | `mock_server_social.c:1559` | 当前场景重载（`25/5` 家族）先由宿主 pending flag 决定是否触发，随后用宿主 grid 作为 `30/1` 坐标并保存。 | 已确认风险；该路径也可导致“加载卡住后位置漂移”。需以当前 session 的 pending/ready 状态取代宿主 pending 和 grid。 |
| P0 | `mock_server_scene_task.c:2279` | 登录/场景 follow-up 完成时，用宿主 grid 覆盖 `currentScene` 的落点，再保存 role。 | 已确认风险；可能表现为重新登录后坐标跳变。应以该 role 已持久化位置或同 client session 的可见位置完成 ready。 |
| P0 | `mock_server_social.c:696` | `WT 2/3` 当前场景完成在没有 `FB4` 时用宿主 grid 改写 scene target，完成后持久化该 target。 | 已确认风险；同场景完成可能使用另一客户端坐标。应由当前 request session 的位置决定，或保留已记忆 target。 |
| P0 | `mock_server_social.c:4869` | 未携带可解析 position 的 moveinfo 先采用宿主 grid，随后才尝试当前 session；并把宿主坐标保存和广播。 | 已确认风险；应把 session-visible/role 放在宿主 grid 前，或对无位置的 opaque moveinfo 不做位置提交。 |
| P1 | `mock_server_scene_task.c:2015` | 传送入口解析会把宿主 grid 作为当前 role scene 的 portal 命中坐标；多出口场景可能解析出错误出口。后续场景完成会保存错误目标。 | 静态确认、未复现。应使用当前 session 移动上传位置或 role 位置；runtime scene 仅在没有 active role 时作为诊断。 |
| P1 | `mock_server_social.c:2980` | `2/10` actor-other-only 的 portal fallback 用 role 场景配宿主 grid 推断自动传送目标。 | 静态确认、未复现。应绑定到该请求 client 的 session 位置，不能读取宿主玩家。 |
| P1 | `mock_server_social.c:4726` → `mock_server_battle.c:5357` | 触怪/挂机战斗按 monster actor id 扫描宿主 scene node 表，可能取得宿主地图同 id 怪物的位置。 | 静态确认、未复现。应统一使用 SCE 战斗 spawn catalog 与当前 role scene。 |
| P1 | `mock_server_scene_task.c:1901`、`:2405` | 宿主场景 pending flag 同时参与“本地传送尚未完成”和“当前场景重载”两个协议门控。 | 静态确认、未复现。应把 pending 生命周期归入 `vm_mock_service_client_session`，并由请求所属 client 读取。 |
| P2 | `mock_server_equipment_npc.c:1390`、`:1421` | `CBE_SCENE_POS_X/Y` 先于 role 坐标；`vm_net_mock_scene_key_name()` 中 `CBE_SCENE_KEY` 先于 role 场景，影响 actorinfo、默认 scene 字段和 `30/1`。 | 当前环境未设置这些变量；它们是全进程调试开关。生产服务应禁止或仅允许未登录诊断模式使用。 |

## 已排除的相似项

1. `g_vm_net_mock_last_scene_change_target`、teleport stone pending、最近 moveinfo source、
   NPC seed 等历史遗留全局变量看似进程级，但
   `vm_mock_service_account_capture/restore()` 已将本审计涉及的场景切换状态保存到
   `vm_mock_service_account_state`，且 `vm_net_mock_service_handle_client()` 用协议 mutex
   串行化该状态域。它们仍是应逐步迁移为显式 request context 的维护债务，但本次
   没有发现跨**不同账号**直接串写的证据。
2. `vm_net_mock_poll_push_if_due()` 的 `Global_R9` 检查只服务本地 emulator client 向
   远端 mock-service 轮询；`g_mockServiceOnly` 时直接返回，不参与入站远端响应。
3. `mock_server_core.c` 中部分 `Global_R9` 读取是下载校验、UI/视觉调试或已有 client
   hook，不是本次服务端业务 builder 的场景权威来源；其中已有 guest-memory 写入的
   legacy hook 不应扩展，后续应单列清理任务而不是借其修复协议问题。

## 建议的处理顺序（未在本次审计中修改）

1. 先对 P0 的战斗 snapshot、登录 follow-up、`2/3` completion 与 moveinfo fallback
   分别抓取“两个账号位于不同场景”的原始请求/响应、`scene_source`、持久化前后
   `account_roles.scene,pos_x,pos_y`。每条路径确认第一次偏离后再改。
2. 提取只读的 `active role/session position` 解析器，明确 source 为
   `packet|session-visible|role-db|unresolved`；协议 builder 不得自行读宿主 grid。
3. 将 session pending 状态作为场景重载/portal 的唯一门控；现有 `Global_R9` pending
   仅可保留为本地 emulator 自动化诊断日志。
4. 将 `CBE_SCENE_KEY` 与 `CBE_SCENE_POS_X/Y` 改为显式测试模式开关，并在有 active
   role 时拒绝覆盖；先补针对性回归，再移除生产路径依赖。

## 未知项

- P0/P1 条目除 unstuck 外尚未采集用户现场同一请求的完整 byte dump 与客户端 parser
  截点；因此本记录不改变其业务行为。
- 当前 Windows 服务的宿主 CBE 在每个条目触发时是否已有有效 `Global_R9` scene node
  取决于本地模拟器生命周期；风险来自“它可被读取且不属于远端请求”，不是假定它
  始终非空。
