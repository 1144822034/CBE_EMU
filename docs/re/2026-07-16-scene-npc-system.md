# 江湖 OL 场景 NPC 协议证据

## 目标

恢复各场景中的交互 NPC。实现必须沿客户端原生网络解析路径创建场景节点：静态交互 NPC 取自实际 `.sce` 资源，只有存在明确运行时/历史协议证据的服务端动态 NPC 才进入窄范围目录。不使用跨场景猜测坐标，也不重新启用已知会破坏场景节点的 `2/10 otherinfo` 或 `2/5 actorinfo` 试验路径。

## 证据记录

```text
phase: scene interactive NPC bootstrap
status: implemented

request:
  wt_kind: 27
  wt_subtype: 11
  objects: 1/27/11，可位于场景完成组合请求中
  key_fields: 请求对象本身无 NPC 行字段
  sample_len: 29（2026-07-16 正式服务运行日志）
  packet_log: builtin-type27-followup

response:
  wt_kind: 27
  wt_subtype: 11
  objects: 1/27/11
  fields: npcnum:u8, npcinfo:raw
  arrays: npcinfo 连续包含 npcnum 行
  strings: displayName, actorResource, scriptName, dynamicDisplayName
  blobs: 每行依次为 tagged-u32 rowId/x/y、四个 len16 字符串、tagged-u32 finalActorId

ida_evidence:
  binary: 江湖OL.CBE（按 binary_name 选择 IDA 实例）
  function: scene_parse_npcinfo_and_spawn_npcs(0x01037998)
  dispatch_case: 27/11
  parser_reads: npcnum、npcinfo；每行 rowId/x/y/name/actor/script/dynamicName/finalActorId
  failure_branch: 场景表尚未初始化时下发实体行可能进入未就绪的形象槽加载路径

runtime_evidence:
  trace_lines: mock_scene_npc_catalog, mock_scene_npc_seed
  handled_source: 首登 scene-sync-poll / builtin-type27-followup / 场景完成响应
  queued_event: event 7
  client_effect: 角色直接登录铸剑谷后创建 20020/20021 两个类型 21 场景节点

negative_evidence:
  missing_or_bad_field: 旧实现混有猜测场景目录；另有 2/10、2/5 的 NPC 试验对象
  observed_failure: 蓬莱 _02 曾在 scene_actor_asset_slot_table_load_entry(0x01044E4C) 中崩溃；因此不能在场景壳创建前发实体 27/11

unknowns:
  - name: more-than-four dynamic NPC labels
    current_value: 仅下发 SCE 顺序中的前四个安全交互 NPC
    why_kept: RegisterDisplayName(0x0100EEE0) 只有四个 36 字节动态名称槽，尚未发现服务端分页或任务状态筛选契约
  - name: unpositioned SCE actors
    current_value: 不下发
    why_kept: 缺少可证明坐标，通常与任务状态或隐藏实体有关
```

## 客户端字段契约

`scene_parse_npcinfo_and_spawn_npcs(0x01037998)` 对每一行严格按以下顺序消费：

1. `rowId:u32`
2. `x:u32`
3. `y:u32`
4. `displayName:string`
5. `actorResource:string`
6. `scriptName:string`
7. `actorResourceKey:string`
8. `finalActorId:u32`

解析器调用 `scene_node_find_or_create(0x0100EFC4)` 创建节点，随后写入 NPC 节点类型 21、名称和最终 actor id。第四个字符串通过 `RegisterDisplayName(0x0100EEE0)` 注册到节点形象槽；运行时已确认这里必须传 `.actor` 资源键，重复传显示名会让客户端请求名为 NPC 中文名的更新文件。

服务端让 `rowId == finalActorId`，ID 由场景名、脚本名、显示名和坐标稳定散列到 `20000..59999`。这不会猜测客户端资源索引；实际形象仍由同一 SCE 行中的 `.actor` 名称决定。

## SCE 数据源

`LoadSceneDataFromStream(0x01006204)` 证明 SCE2 是场景模板和摆放数据源。服务端复用现有资源解压入口读取 `web/fs/JHOnlineData/*.sce`，只接受同时满足以下条件的记录：

- 字段 3 为 `.actor`；
- 字段 4 为 `.xse`；
- 字段 1 有非空 GBK 显示名；
- 记录中有明确的坐标 token；
- 字符串长度不超过客户端 30/30/32 字节固定缓冲。

离线盘点在 16 个场景中找到 31 个满足条件的交互 NPC。例如：

- `00蓬莱仙岛_04.sce`：药师 `(166,280)`、绝刃-幻剑 `(478,91)`、药王-鬼道 `(239,75)`；
- `00_蓬莱仙岛01.sce`：大侠郭靖 `(310,220)`、郭芙蓉 `(194,475)`。

缺少脚本、名称或坐标的传送石、隐藏任务实体和战斗刷怪记录不会被误当作交互 NPC 下发。

## 服务端动态 NPC 目录

`00蓬莱仙岛_02.sce`（蓬莱-铸剑谷）的真实 SCE 只有场景道具和传送口，不含 `.actor/.xse` NPC 行，因此不能靠 SCE 扫描恢复该场景角色。历史 `27/11` 协议目录与当前场景校正共同确认了两项服务端动态角色：

- `20020`：欧冶子（铁匠），`n_blacksmith.actor`，坐标 `(338,125)`；
- `20021`：小猴子，`e_monkey.actor`，坐标 `(376,125)`。

客户端 `0x01037998 -> 0x0100EFC4` 将这两个值原样写入节点场景像素坐标；解码后的 `00蓬莱仙岛_02.map` 为 512×512，`y=125` 是铸剑屋门外的可行走带，两个 NPC 以 38 像素间隔排在门右侧。这两项在构建 `27/11 npcinfo` 时先于 SCE 行合并，仍受四个动态名称槽的总上限约束。日志以 `source=service-dynamic` 区分，避免把它们误记成 SCE 扫描结果。`bin/JHOnlineData/npcs.json` 中带 `.scex` 键的实验数据不是运行时目录，服务端不读取它。

欧冶子的模型字段必须使用客户端资源包中的 `n_blacksmith.actor`。本地角色资源预览显示该资源为持锤铁匠，而此前误用的 `n_swordmaster.actor` 是红衣剑师；原始 SCE 导出中的铁匠行也统一使用 `n_blacksmith.actor`。`27/11` 的第四个字符串会直接进入客户端资源解析链，因此这里不能用职业近似模型代替。

### 临安-南宣门（2026-07-17 场景归属校正）

当前真实资源链给出了以下交叉证据：

- `sMap.dsh` 第 47 行：`c04临安府_01.sce`，别名“临安-南宣门”；第 55 行 `c04临安府_09.sce` 才是“临安-北宣门”；
- 王朝、马汉、胡斐属于南宣门，先前把三人注入 `_09` 是场景归属错误，北宣门不再追加这三行；
- `c04临安府_01.sce` 是旧版南宣门资源，其中嵌入的宋兵乙、守门卫兵甲、王大胆、守门卫兵属于旧目录。当前 `27/11` 不再把这些旧演员行与服务端当前目录混合；
- 历史客户端场景资源仍可确认王朝绑定 `n_solider2.actor / 04临安王朝.xse`，马汉绑定 `n_solider1.actor / 04临安马汉.xse`；胡斐保留已校正的 `n_man1.actor / 04临安胡斐.xse`；
- `task.dsh` 进一步确认王朝、马汉分别是悬赏任务交付者，胡斐是“传家宝”任务的给予者和交付者；三个 `.xse` 文件中的人物名与对话也一致。

服务端为该场景追加三行稳定目录：

- `20090` 王朝：`(172,132)`；
- `20091` 马汉：`(228,132)`；
- `20092` 胡斐：`n_man1.actor`，`(264,304)`。

旧版南宣门地图为 304×416。王朝、马汉的坐标仍位于上方广场；胡斐原北宣门坐标 `x=304` 正好落在南宣门地图右边界，因此收回到图内的 `(264,304)`。客户端 `RegisterDisplayName(0x0100EEE0)` 只有四个 36 字节名称槽，当前三人必须优先且独占本场景的服务目录，不能再拼入四个旧 SCE 演员导致越界或静默丢失。运行时仍不读取 `tmp/all_sce_export`，该历史导出只作为人工证据。

画面中的建筑来自旧版 `04临安府_01.map` 本身，不是 `27/11` 动态 NPC 行带出的对象；服务端没有修改或附加 map/SCE 建筑层。

## 生命周期与防重复

- 初始场景切换 `2/3` 仍返回空 `27/11`；此时客户端场景节点表可能尚未创建。
- 真实 `30/1` 场景进入对象会重新标记当前场景 NPC 待刷新；角色选择的 subtype-6 首登路径也会显式重新标记，因为它不会经过 `30/1` builder。
- 首登的 `12/1` 任务组合响应在本机可能超过客户端 1.5 秒传输等待，因此不再把 NPC 实体埋在该大响应中；场景标记为 ready 后的第一次 scene-sync poll 单独下发一次 `27/11`。
- NPC 一次性生命周期比较统一忽略 c 前缀场景键的可选 `.sce` 后缀。例如数据库角色场景可能保存 `c04临安府_01.sce`，而 ready session 使用 `c04临安府_01`；严格 `strcmp` 会在 poll 前错误清除 pending，使首登南宣门不下发目录。
- 当前场景完成、重复完成、传送石完成、post-enter 完成、task-subset 延迟完成，以及客户端明确的 `27/11` 跟进请求，都会调用同一个场景目录 builder。
- 同一 session、同一场景只下发一次实体列表；后续重复请求返回空 `27/11`，避免重复创建节点或重复注册动态名称。
- 切换/重新进入场景后由 `30/1` 重新武装，允许再次建立该场景的 NPC 节点。

## 验证

- `make -j2`：铸剑谷动态目录以及临安南/北宣门归属校正均已通过构建。
- 正式 mock 服务已按原参数重启并由 PID `20924` 监听 `127.0.0.1:19090`；启动日志为 `tmp/mock-service-19090.20260717-161814.stdout.log`。
- 直接登录铸剑谷的自动化实测输出 `mock_scene_npc_seed phase=startup-scene-sync-poll ... npcnum=2`，poll 响应 `resp=245` 在 `network_ms=1168` 内到达客户端；场景节点表随后出现 `20020 pos=(338,125)`、`20021 pos=(376,125)`，两者均为 `prompt=21 active=1`，无地址异常或断言。
- 先前在北宣门 `_09` 下发三人的日志只证明 `27/11` 行结构和模型可被客户端消费，现作为错误场景归属的负面证据保留，不再代表当前目录位置。
- 南宣门首登复测记录 `scene=c04临安府_01 source=service-dynamic actors=3 rows=3 dynamic=3 truncated=0 npcinfo_len=250`，旧 SCE 演员未计入 `total/rows`；客户端节点表稳定出现 `20090 (172,132)`、`20091 (228,132)`、`20092 (264,304)`，均为 `kind=1 prompt=21 active=1`。
- 客户端画面标题为“临安-南宣门”，显示两名守卫和胡斐，持续约 50 秒无地址异常、算术异常或断言。验证日志为 `tmp/mock-service-19090.20260717-190244.stdout.log`，截图为 `bin/multiplayer-data/player-1/autotest/screens/000024_00096190.bmp`。
