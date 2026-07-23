# 2026-07-23 脱离卡死跨场景位置重置

## 状态

已修复并以服务端协议回归验证。此记录补充
[2026-06-27-unstuck-current-scene-reload.md](2026-06-27-unstuck-current-scene-reload.md)：
`12/3 + 16/3` 的客户端契约没有改变，修复的是该契约中目标场景的权威来源。

## 可重复触发条件

1. 远端账号已选择角色，并且角色持久化位置在非出生场景（回归用例使用
   `01桃花岛_02.sce`）。
2. 服务端宿主 CBE 运行时仍停留在出生场景，或以 `CBE_SCENE_KEY` 指向
   `c00蓬莱仙岛_01.sce`。
3. 客户端设置菜单确认“脱离卡死”，发送 `WT 0/12/3 { id }`。

故障时客户端会进入场景加载，而服务端把角色位置保存为宿主的出生场景，
所以重新进入后表现为位置重置。该保存发生在响应建立后，而非客户端崩溃
PC 处。

## 客户端与协议证据

按 `binary_name=mmGameMstarWqvga.cbm` 选择 IDA 实例后复核：

- `0x5BCA` 是设置菜单确认处理；“脱离卡死”构造 `0/12/3`，字段 `id`。
- `sub_11CE(0x11CE)` 扫描响应对象；`16/3 result=2` 才进入场景处理。
- `sub_BCC(0x0BCC)` 读取 `scene`、`posinfo`、`exitid`，调用场景进入并清除
  等待状态。

因此该请求的正确服务端响应仍是：

```text
12/3 { result: typed-u8 1, id: typed-u16 request-id }
16/3 { result: typed-u8 2, scene, posinfo, exitid }
```

不能为避免加载而丢弃 `16/3`、伪造成功或从客户端内存强制改坐标。

## 首个偏离与根因

`vm_net_mock_get_current_scene_unstuck_target()` 曾调用
`vm_net_mock_current_scene_name()`，后者优先读取 `Global_R9` 的场景对象。
这是承载 mock service 的本地模拟器全局状态，既不按请求客户端隔离，也不是
已认证角色的持久化状态。随后 unstuck builder 调用
`vm_net_mock_save_player_pos_state(target.scene, ...)`，将这个错误场景写入该远端
角色；这就是出生点重置的第一次错误状态。

已排除的方向：`12/3` ACK 字段、`16/3` 对象顺序和 SCE 最近入口落点选择都符合
客户端解析契约；它们不能解释“仅跨场景时变为宿主出生地图”的现象。

## 修复

- `vm_net_mock_current_scene_name()` 在已有选中角色时优先使用角色的合法场景；
  `CBE_SCENE_KEY` 与 `Global_R9` 仅保留为没有角色的本地诊断回退。
- unstuck 目标显式使用当前角色的场景和坐标；如果已完成进入同一场景，则使用
  该客户端 session 的可见位置作为“最近入口”距离参考。只有不存在合法角色时
  才能读取宿主运行时 grid。
- 新增日志 `scene_source=role-db|runtime-fallback|default-fallback`，使后续现场
  记录能区分权威来源；目标仍由同一场景 `.sce` 的最近入口/中心生成。

这保持原有场景进入生命周期：直接进入标记完成，再保存同一请求角色的目标位置；
不会再投递第二个带位置的重入响应。

## 验证

`tmp/unstuck-current-scene-authority-regression.php` 建立角色位置为
`01桃花岛_02.sce` 的夹具。测试进程故意设置
`CBE_SCENE_KEY=c00蓬莱仙岛_01.sce`，登录并选择该角色后发送真实形状的
`WT 0/12/3`。断言：

1. 响应同时含 `12/3 result=1` 与 `16/3 scene`；
2. `16/3.scene` 是 `01桃花岛_02.sce`，不是出生场景；
3. MySQL `account_roles.scene` 同样保持该场景，并保存非零安全落点。

该回归专门使旧的“宿主场景优先”实现失败；它不是只检查客户端没有崩溃。

2026-07-23 实测结果：`make -j2` 通过；测试服务以
`CBE_SCENE_KEY=c00蓬莱仙岛_01.sce` 启动后，回归输出
`scene=01桃花岛_02.sce landing=160,342 response=106`。服务日志记录
`mock_unstuck_target ... scene_source=role-db ... source=sce-nearest-entry`，并确认
`builtin-settings-unstuck` 返回 `12/3 + 16/3`。因此既验证了原始请求分支，也验证了
错误宿主场景不会再写入角色。

## 2026-07-23 回归：`16/2` 误判后的第二次出生场景进入

Status: fixed and verified in an isolated mock-service process.

### 触发与运行时证据

用户在非出生场景执行“脱离卡死”后仍会卡住，随后角色回到出生场景。最新
`mock-service-19090.20260723-173710.stdout.log` 中，同一远端会话的首次偏离已可见：

```text
mock_unstuck_target scene=00蓬莱仙岛_02.sce ... pos=(128,57) ...
mock_settings_unstuck_16_2 scene=00蓬莱仙岛_02.sce ... response=16/2-direct-enter
mock_teleport_stone_transfer subtype=3 exit=0 scene=c00蓬莱仙岛_01 ...
```

第一条目标仍来自当前会话/角色；出生场景不是它产生的。紧随其后的 `16/3`
却被 `builtin-teleport-stone-transfer` 接管，
`vm_net_mock_get_teleport_stone_target()` 在没有已确认传送石目标时初始化为
`vm_net_mock_default_scene_name()`，而该 builder 又立即保存该目标。故第一次把角色
写成出生场景的是 **第二个错误路由的 `16/3` 响应**，不是原有
`12/3` 脱离目标的场景来源。

### IDA 复核

按 binary name 选择 `mmGameMstarWqvga.cbm` 后复核：

- `0x5BCA` 的“脱离卡死”确认路径构造的是 `WT 0/12/3 { id }`，不是 `16/2`。
- `0x11CE` 对 `16/2`：`result==2` 进入传送确认；其他值会调用 `0x0BCC`。
- `0x0BCC` 读取 `scene`、`posinfo`、`exitid` 并直接进入场景。
- `0x11CE` 对 `16/3`：仅 `result==2` 才调用 `0x0BCC`。

再按 binary name 选择 `江湖OL.CBE`，复核
`scene_runtime_init_and_sync(0x0101359C)`。该函数在 scene switch 后：

```text
parserState 2 or 3:
  alloc_outgoing_game_event(2, 1, 16, 3)
  put_i16("exitID", currentPosX)
  put_u8("type", 0)
  copy current target coordinates to the active node
```

也就是说，direct-enter 后出现的单对象 `16/3` 是客户端的场景 runtime 同步/确认
事件。它的 `exitID` 是当前位置的 i16，不是传送石条目；该编码亦解释了服务端
`get_object_u32_field("exitID")` 读出零、日志显示 `exit=0` 的现象。

此前实现缺的是这个 direct-enter 后的 `16/3` 分流，而不是场景坐标来源。泛化的
传送石 detector 仅按 `16/3 + (exitID|type)` 命中，未区分 `type=0` 的 runtime 同步；
它把当前位置 i16 当成传送石 `exitID`，又在没有可用确认 target 时调用
`vm_net_mock_get_teleport_stone_target()`，将默认出生场景写进角色状态。

### 修复与验证

- 在传送石 detector 之前，精确识别单对象
  `1/16/3 { exitID=i16, type=u8(0) }` 的 scene-runtime 同步事件，返回零对象 WT
  ACK，不创建 scene target、不保存位置、不触发场景进入。
- 传送石 `16/2/16/3` 确认链仍要求其记录的确认 target，且其 `type` 不是此 runtime
  同步值；不会改变已有传送石语义。
- `tmp/unstuck-current-scene-authority-regression.php` 补充了
  `type-only 16/2 direct-enter -> type=0 16/3` 回归：断言第二个响应是空 WT
  (`WT`, packetLength `5`, objectCount `0`)，而 MySQL 中角色场景仍是原非出生场景。

2026-07-23 隔离服务实测：新回归输出
`unstuck direct runtime ack regression passed scene=01桃花岛_02.sce response=5`；既有
`0/12/3` 回归同样输出
`unstuck authority regression passed scene=01桃花岛_02.sce landing=160,342 response=106`。
服务端日志分别命中 `builtin-scene-runtime-position-ack-16-3` 和
`builtin-settings-unstuck`，没有再由该 `type=0` 请求调用传送石 target/resolver。
同时 `make -j2` 与 `make boundary-check` 均通过。
