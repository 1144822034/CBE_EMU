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
