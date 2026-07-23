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

## 2026-07-23 现场复现：44 字节复合 runtime follow-up

Status: fixed and verified against the captured live packet shape.

### 触发、首个偏离与证据

非出生场景选择“脱离卡死”后，服务端先正确返回 direct-enter：

```text
mock_unstuck_target scene=00蓬莱仙岛_02.sce ... scene_source=role-db
mock_settings_unstuck_16_2 scene=00蓬莱仙岛_02.sce ... response=16/2-direct-enter
```

随后客户端实际发送 44 字节 WT。临时只读取证得到其完整结构：

```text
1/16/3 { exitID: 00 04 00 00 2f 44, type: 00 01 00 }
1/27/11 {}
1/7/42  {}
```

`WT` 请求包没有 object-count 字节；第四字节后的 `1` 是第一个对象的 major。上一版
只覆盖了单对象短包，因而该复合包没有命中 runtime handler，落入宽泛的
`builtin-teleport-stone-transfer`。该 builder 把 `exitID=12100` 当作传送出口，在没有
确认传送目标时构造默认出生场景，并以 `teleport-stone-target` 原因写入角色位置。
这才是本问题中角色回到出生点的第一个错误状态。

### 客户端契约交叉验证

按 binary name 选择 IDA 实例后复核：

| binary | function/address | evidence |
| --- | --- | --- |
| `mmGameMstarWqvga.cbm` | `0x5BCA`, `0x11CE`, `0x0BCC` | “脱离卡死”发 `0/12/3`；direct `16/2` 的非传送结果进入场景，`0x0BCC` 读取 scene/posinfo/exitid。 |
| `江湖OL.CBE` | `scene_runtime_init_and_sync` / `0x01012FB4` | parser state 2/3 发 `1/16/3`，写 `exitID` 与 `type=0`；随后请求 `1/27/11` 与 `1/7/42`。 |
| `江湖OL.CBE` | `scene_parse_npcinfo_and_spawn_npcs` / `0x01037998` | `27/11` 的 `npcnum`/`npcinfo` 创建或刷新场景 NPC 节点。 |

IDA 反编译中使用 i16 写入例程描述该值的语义；本次真实线包将该小坐标序列化为
`typed-u32`。因此实现按已捕获的 wire encoding 匹配，同时限制数值不超过 `u16`。

### 修复

- 在所有传送石 detector 之前新增精确 detector：仅接受长度 44、首对象
  `1/16/3 { exitID=typed-u32<=65535, type=typed-u8(0) }`，且后续必须依次是空
  `1/27/11`、空 `1/7/42`，没有其他对象。
- 响应只返回该客户端已经请求的 `27/11 { npcnum, npcinfo }` 与
  `7/42 { booknum, booksinfo }`。由于 direct `16/2` 已创建新的 scene shell，响应前
  正确重置 27/11 的一次性 NPC catalog 生命周期。
- 此 handler 不创建 scene target、不保存角色坐标，也不发送任何 `16/3` 场景进入。
  对合法但没有权威场景的异常状态返回空 WT 并记录 `unresolved`，绝不回落到传送石。
- 旧的单对象 runtime ACK handler 保留给它实际覆盖的短包；该合成测试不再被用作
  现场 44 字节行为的验证。

### 验证

`tmp/unstuck-current-scene-authority-regression.php` 新增 44 字节复合包回归。测试先让
角色位于 `01桃花岛_02.sce`，而服务进程显式带出生场景 `CBE_SCENE_KEY`，然后发送
真实对象序列。断言响应包含 `27/11.npcnum` 与 `7/42.booknum`、不含场景 `16/3`，并且
MySQL 的角色场景仍是 `01桃花岛_02.sce`。

2026-07-23 隔离服务实测输出：

```text
unstuck direct runtime compound follow-up regression passed scene=01桃花岛_02.sce response=66
unstuck direct runtime ack regression passed scene=01桃花岛_02.sce response=5
unstuck authority regression passed scene=01桃花岛_02.sce landing=160,342 response=106
```

对应服务日志命中
`builtin-scene-runtime-direct-enter-followup-16-3-27-11-7-42`，并记录
`action=no-scene-target-or-position-save`；该测试日志中没有任何
`mock_teleport_stone_transfer` 或 `teleport-stone-target` 写入。`make -j2` 与
`make boundary-check` 均通过。
