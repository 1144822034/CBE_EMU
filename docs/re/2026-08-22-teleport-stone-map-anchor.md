# 场景传送石：地图目录与石头锚点落点

## 触发、偏离与根因

场景内传送石的协议链保持为 `WT 1/16/1` 列表、`WT 1/16/2 { exitID }` 选择、
`16/3` 确认，以及随后既有的延迟 `30/1` 场景进入。客户端的 `16/1` parser 读取
`exitinfo` 的计数和行数据；`16/2` 的 `exitID` 必须能回解到同一个目录。

此前目录把任何字节中出现 `n_telestone` 的 SCE 都列入；没有精确 `sMap.dsh` 行时再
分配合成 ID。更关键的是，选择目标后调用通用的“合理出生点”解析，后者选择场景入口或
中心，而不是传送石所在位置。这是“地图中没有的地点也可选择”以及“传送后离石头很远”的
第一处偏离。

## 资源与解析证据

- `sMap.dsh` 的第 0/1/2 列为行 ID、精确 SCE 键和显示别名。只有 SCE 键逐字匹配的行
  才能作为本功能的 `exitID`。
- 真实 SCE 中 `n_telestone.actor` 有两种发货结构：
  - Actor 自身在 field-3 资源名后携带 type-2 点坐标；
  - 静态 kind-5 放置记录保存 `(x,y)`，其后的局部 Actor 引用
    `n_telestone.actor`，但该 Actor 本身不重复位置。
- 以太乙峰 `11终南山_02.sce` 为例，第二种结构的 kind-5 记录位于偏移 393，坐标为
  `(68,208)`，紧邻的传送石 Actor 位于偏移 422。该结构已用项目内 SCE 检查器交叉验证，
  运行时只从原始 SCE 资源加载，不依赖导出的临时文件。
- `vm_net_mock_adjust_safe_player_pos_for_scene()` 只在锚点恰好落到 SCE 边界传送口的
  触发矩形中时，将坐标移出该矩形；正常情况下保持石头锚点不变。这保留防卡死边界，
  不会退回场景中心。

## 修复

`vm_net_mock_collect_teleport_stone_destinations()` 现在只收录同时满足以下条件的目的地：

1. SCE 可通过正常服务端资源加载/解压路径解析到真实的 `n_telestone.actor` 与非零锚点；
2. `sMap.dsh` 存在该 SCE 键的精确行，行 ID 作为列表和 `16/2` 的唯一 `exitID`；
3. 地图别名非空，可作为 `exitinfo` 行标签。

未登记的 SCE 不再获配合成高位 ID，也不会出现在列表中。`16/2` 从同一目录取目的地、
传送石锚点和地图 ID；未知 ID 仍明确拒绝。原 `CBE_TELEPORT_STONE_SCENE`、
`CBE_TELEPORT_STONE_X` 和 `CBE_TELEPORT_STONE_Y` 覆盖已移除，不能再绕过上述约束。

本资源快照下，目录序列化为 15 个地点。太乙峰仍使用 `exitID=90`，选择后的最终落点为
`(68,208)`（除非该锚点与场景入口触发矩形重叠而需要最小安全移位）。世界地图的
`16/4` 子场景选择不属于本次场景传送石目录，保持原有 `wMap/sMap` 契约。

## 验证

资源回归 `scripts/teleport-stone-scene-catalog-regression.c` 不启动监听器、不连接 MySQL、
不写角色数据。它重建生产目录并断言：

- 每个列表行都有精确 `sMap` 行、非零石头锚点和真实 `n_telestone.actor`；
- 未登记的 `00蓬莱仙岛_04.sce` 不会出现；
- `16/1 exitinfo` 的 typed-u8 计数、行数和目录一致；
- 构造真实形状的 `WT 1/16/2 { exitID: 90 }` 后，太乙峰目标仍为准确 SCE，且坐标与
  传送石锚点（经过同一安全修正）一致。

本次执行结果：

```text
teleport-stone-scene-catalog regression passed: entries=15 taiyi_exit=90 taiyi_stone=(68,208)
```

已运行 `make -j2`，客户端和服务端均成功编译、链接。首次链接曾因
`bin/jh-online-server.exe` 被现有进程占用而失败；未停止或重启该进程，待占用自行释放后
重试构建成功。仍需用持有传送石的测试角色手动验收列表滚动和完整
`16/2 -> 16/3 -> 30/1` 画面流程。
