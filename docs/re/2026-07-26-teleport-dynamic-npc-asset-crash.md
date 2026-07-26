# 瞬移后动态 NPC 资源名崩溃

日期：2026-07-26

## 触发步骤

1. 角色位于其他场景，使用传送石回到 `c00蓬莱仙岛_01.sce`；
2. 客户端完成当前场景 screen 的移除，并处理服务端场景进入后的 `27/11`
   NPC 目录；
3. 客户端在 `FindOrAddAssetName(0x0100D262)` 中登记
   `n_girl.actor` 时访问空地址，最终由 `hookRam.c:112` 断言终止。

现场日志的寄存器和对象数据同时包含：

```text
pc=0100d2aa r0=0
r5: n_girl.actor\0 ... task2.xse\0
```

`0x0100D2AA` 位于 `FindOrAddAssetName` 调用内部的资源名记录分配之后、写入
资源名之前。它是错误场景目录内容进入客户端后的症状，而不是 screen 栈应被
修改的证据。

## 已确认链路

```text
传送石确认/进入
  -> 25/5 场景完成 follow-up
  -> scene-task 追加 27/11 npcinfo
  -> 客户端 scene parser / LoadSceneAsset
  -> FindOrAddAssetName("n_girl.actor")
  -> 地址 0 访问
```

本机服务日志的同一轮传送记录为：

```text
mock_scene_npc_catalog scene=c00蓬莱仙岛_01
source=service-dynamic actors=3 selected=3 rows=3
mock_scene_npc_seed ... npcnum=3
```

MySQL `server_dynamic_npcs` 对该精确场景键的读取结果包含：

| actor_id | 名称 | actor | XSE |
| --- | --- | --- | --- |
| 30001 | 郭芙蓉 | `n_girl.actor` | `task2.xse` |
| 30002 | 白展堂 | `n_boy.actor` | `task1.xse` |
| 30005 | 大侠郭靖 | `n_warriormaster.actor` | `task0.xse` |

这也解释了现场 `n_girl.actor/task2.xse` 字节：它不是客户端或场景屏幕随机
生成的数据，而是服务端动态 NPC 行原样进入 `27/11` 的结果。

## 客户端和服务端契约证据

- 保存的江湖 OL 反编译 `tmp/ida_full_jh_actor_update/decompiled.c` 中，
  `FindOrAddAssetName(0x0100D262)` 将每个新资源名登记到场景资源表；
  `LoadSceneAsset(0x0100D3EE)` 在加载 Actor 前必经该函数。
- 当前没有打开 `binary_name=江湖OL.CBE` 的 IDA 实例（仅有
  `MT6252_CH.bin`），因此本次没有以硬编码实例 ID 替代运行时取证；上述
  已保存反编译和现场 PC 共同作为客户端路径证据。
- `docs/re/2026-07-18-xse-scene-task-catalog.md` 已记录：旧铜雀台的
  `n_girl.actor` 是当前客户端不兼容资源，正确兼容映射为
  `n_woman1.actor`。
- 现有 SCE 解析器已经实施此映射；但是
  `vm_net_mock_dynamic_npc_row` 和 `vm_net_mock_dynamic_npc_admin_save`
  只验证原始文件是否存在，因此可把 `n_girl.actor` 直接放入动态目录。
  这是首个违反的资源契约。

已排除的假设：

- 不是传送目标或落点问题：服务日志的目标场景为正确精确键
  `c00蓬莱仙岛_01.sce`，崩溃对象也已是该场景动态目录；
- 不是 `dp_change` 的屏幕栈错误：它发生在进入资源登记前，且不能解释
  `r5` 中的 actor/XSE 对；
- 不是应重放/延迟 `27/11`：该做法已被 2026-07-19 的资源生命周期取证排除，
  延迟目录不会分配客户端资源名行。

## 根因与修复点

根因：动态 NPC 的两个入口（MySQL 启动加载、后台即时保存）把
`n_girl.actor` 当作普通可选 Actor，允许这条不适用于动态 `27/11` 生命周期的
设置进入铜雀台目录。

修复位于动态 NPC 配置层，而不是客户端或传送流程：数据库迁移将旧记录写为
已验证的 `n_woman1.actor`；动态加载器对未迁移旧行运行时停用；后台下拉框、保存
和启用操作均拒绝 `n_girl.actor`。SCE 资源继续只做服务端权威资源存在性检查，
不修改原始名称。这样不需要 runtime alias，也不改变客户端内存、screen 生命周期、
传送坐标或网络事件顺序。

## 验证计划

1. 执行数据迁移后，c00 动态目录不再含 `n_girl.actor`；
2. 通过传送石重现原路径，确认 `27/11` 保留三名 NPC，但资源名为
   `n_woman1.actor`，不重放额外目录；
3. 复测首次登录、离开再返回、后台重新保存该 NPC 三条路径；
4. 执行 `make -j2`，并对回归服务响应检查不含旧资源名。

## 本次实现与验证

- 删除运行时 `n_girl.actor -> n_woman1.actor` alias；SCE parser 不再重写 Actor。
  动态 NPC 只接受明确支持的 Actor，后台也不会再展示或保存 `n_girl.actor`。
- 本机迁移实际更新两条旧记录：启用的 c00/30001（郭芙蓉）和停用的
  c04/30441（测试）。两条现在均保存为 `n_woman1.actor`；位置、名称、XSE 和
  启用状态未改变。
- 使用迁移后的数据库启动服务，日志为 `rows=18 skipped=0 quarantined=0`，且不再
  输出 actor alias；c00 的三行动态目录校验仍通过。
- `make -j2` 通过，服务已重启并监听 `127.0.0.1:19090` 与 `127.0.0.1:19091`，
  标准错误输出为空。

仍需使用 CBE 客户端复测原始“传送石返回铜雀台”路径，确认场景中保留郭芙蓉、
白展堂和郭靖三名 NPC，且不再出现 `0x0100D2AA` 地址异常。

## 动态配置复核（兼容层移除准入）

本机数据库的启用动态记录中，只有下面这一条会把 `n_girl.actor` 放入当前
场景目录：

```text
scene=c00蓬莱仙岛_01 actor_id=30001 enabled=1
name=郭芙蓉 actor=n_girl.actor xse=task2.xse
```

另有 `c04临安府_04/30441` 的同名测试记录，但它已停用。全量解析 196 个 SCE
得到两处 `n_girl.actor`：旧铜雀台 `00_蓬莱仙岛01.sce` 的郭芙蓉，以及
`00蓬莱仙岛_03.sce` 的瑛姑；前者已由 c00 的精确目录策略过滤，后者没有可由
当前服务端交互目录下发的 XSE。因此不存在一条需要服务端动态 NPC 响应继续替换
`n_girl.actor` 的有效 SCE 路径。

`n_girl.actor` 和 `n_woman1.actor` 都能通过 Actor/GIF 结构检查，区别不是资源
文件损坏，而是前者不能作为当前客户端动态 NPC 生命周期中的可配置模型。正确的
修复是配置迁移：把已有动态记录改为 `n_woman1.actor`，并在动态 NPC 后台与
数据库加载校验中明确拒绝 `n_girl.actor`。完成后删除运行时 alias；旧数据库若
没有执行迁移会被跳过并记录配置错误，而不是重新引入崩溃。
