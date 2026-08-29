# player-1 黑木崖麒麟殿遇怪前场景重载

## 状态

`ready-to-implement`（2026-08-28）。本记录只处理服务端的 WT 场景进入契约；宿主仍只把 CBE
登记的 callback/context 与不透明响应字节投递给固件，不改变客户内存、寄存器、PC/LR、screen
或输入队列。

## 复现与原始证据

- 客户端：`bin/multiplayer-data/player-1`，连接 `127.0.0.1:19090`，服务端会话
  `847884b8`，角色 `10093`。
- 地图键：`23蟠龙寨_12.sce` 是黑木崖-麒麟殿（另见
  `2026-08-14-paid-instance-map-access.md`）。
- `bin/server_out.txt` 的首次偏离：在外层 `23蟠龙寨_11.sce` 的门户触发矩形
  `(124,387)`，服务器四次以 SCE edge portal 解析出目标 `23蟠龙寨_12.sce`、落点
  `(119,64)`。紧接着第一个真实 `WT2/3` 被记录为
  `mock_scene_download_ack ... response=scene-ack-without-posinfo`；随后
  `mock_scene_enter_defer ... missing=- keep_pending=1`。
- 此时 player-1 的客户端仍报告
  `scene_lifecycle_moveinfo_ack ... ready=0 pending=1 visible_scene=23蟠龙寨_11.sce`，
  但服务器已将目标记为 `23蟠龙寨_12.sce`。客户端随后把场景节点的遇怪 `WT4/1` 发出，服务端
  构造 `WT4/5`；由于目标场景壳尚未收到一次位置型 `WT30/2`，客户端没有进入战斗，且继续重发
  相同门户请求，形成重载循环。
- 同一会话的首次地图石进入已收到 `23蟠龙寨_12.sce`、`e_dongfang.actor`、
  `e_dongfang.gif` 与 `e_believerS.actor` 的 `WT18/7`；启动内容协商记录为
  `release=0/0 pending=0`。因此后续门户处的 `missing=-` 不是一个可由协议证明的客户端资源缺失。
- 纯资源回归 `scene-battle-monster-field18-regression` 已在当前资源根执行通过：196 个已发布
  SCE2 实体表均可由生产解析器读取（唯一历史排除项为 `09华山_02.sce`）。这排除了把麒麟殿
  场景文件整体损坏当作下载等待的理由。

## 客户端与协议契约

- 已有静态证据指出 `JianghuOL.CBE:0x01039770` 消费 `WT30/2`，而带 `posinfo` 的首次结果才会
  建立目标场景；后续完成只能是不带坐标的 `30/2`，不能重复进入。
- `mmBattle:HandleBattleStartMsg`（`0x66CC`）消费 `WT4/5` 时要求已存在的场景 kind-2 节点。
  因而 `WT4/5` 不是场景完成的替代物。
- `docs/re/2026-08-22-instance-monster-resource-reentry.md` 已确立普通门户的顺序：首次
  `WT2/3` 发一次位置型 `30/2`；若有**明确**的 manifest 资源 pending，才等待对应的 `WT18/7`，
  并由 `WT6/1` 发一次无坐标完成。

## 根因

`vm_net_mock_prepare_scene_enter_resources()` 将
`vm_net_mock_scene_client_content_ready()` 的任意 `false` 都写成
`target.needsSceneDownload=true`。但该 helper 除了可验证的内容 manifest pending，也可能因为服务器
对场景实体表的探测无法给出结论而返回 false；后者没有资源名，因此日志为 `missing=-`。这把服务端
的探测不确定性伪装成客户端仍在下载，令普通门户跳过首次位置型 `WT30/2`，永久保持 pending。

## 修改和验证计划

1. 只有带有明确资源键的 client-content pending 可以把目标标为 `needsSceneDownload`；不能把
   `missing=-` 映射为下载状态。无具体缺失项时保留正常的门户进入字节，让固件按其资源路径继续。
2. 为 `23蟠龙寨_11.sce -> 23蟠龙寨_12.sce` 加入无监听、无数据库的 WT 回归：首个 `WT2/3`
   必须有且仅有一个位置型 `30/2`；后续 `WT6/1` 最多产生一个无坐标完成；遇怪响应仅在场景完成后
   允许。
3. 修改后运行 `make -j2`、对应回归与既有 SCE2 实体表回归。人工复测应从 player-1 进入麒麟殿，
   确认不再重复 `WT2/3`，再与 actor 103 碰撞并确认 `WT4/5` 后出现 battle screen。

## 实施与验证（2026-08-28）

- `vm_net_mock_prepare_scene_enter_resources()` 现在只会在 helper 给出非空、精确的 manifest
  资源键时设置 `needsSceneDownload`。无资源名的检查结果会记录
  `mock_scene_content_probe_inconclusive`，并交回正常的场景进入契约；它不会伪造资源已安装，也
  不会干预客户端 callback、screen 或战斗状态。
- `scripts/scene-transition-entry-contract-regression.c` 新增两条纯服务端夹具：
  - 历史非标准 SCE 的不确定 probe 必须仍得到一次位置型 `WT30/2`；
  - player-1 的精确 `23蟠龙寨_11.sce@(124,387) -> 23蟠龙寨_12.sce@(119,64)` 门户请求必须
    得到一次、且仅一次位置型 `WT30/2`，目标不能标记为下载中。
- 已执行且通过：
  - `make -j2`；
  - 编译并执行 `obj/server/scene-transition-entry-contract-regression.exe`（无 listener，MySQL
    目标为拒绝连接的隔离端口；进程退出为 0）；
  - `obj/server/scene-battle-monster-field18-regression.exe`（196 个发布 SCE2 实体表通过）。

仍需 player-1 的人工协议复测：使用新构建启动隔离服务，进入麒麟殿后检查服务端先出现
`mock_scene_change_initial_enter ... 30/2-posinfo`，不再出现同一门户的
`mock_scene_download_ack ... missing=-`；再碰撞 actor 103，确认 `WT4/5` 后进入 battle screen。

## 已排除项与风险

- 不是门户解析或落点错误：运行日志给出匹配 trigger rect 和 `(119,64)` 的 SCE 落点。
- 不是用服务端强制进战斗可解决的问题：那会绕过客户端的 `WT30/2 -> scene node -> WT4/5` 契约。
- 若复测出现有名称的 manifest pending，仍应保持等待该名称的资源完成；本修复不把真实 pending
  当作 cache hit。
