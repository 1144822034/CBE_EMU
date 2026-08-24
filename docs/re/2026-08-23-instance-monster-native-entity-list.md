# 副本战斗怪原生实体集合契约

Date: 2026-08-23

Status: unresolved collision dispatch; fireball visibility and quantity confirmed

## 1. 当前卡点

- 可见现象：后台部署的 `测试地图.sce` 可以下载并进入，但指定战斗怪没有出现在场景中。
- 原生对照：进入 `00蓬莱仙岛_02.sce`（蓬莱-铸剑谷）后，药师附近会出现小猴子的
  `e_ghostfireR.actor` 火团；触碰后以怪物 ID `1000` 进入小猴子战斗。
- 本轮最小目标：让部署器把新增 kind-3 记录放进客户端实际消费的 SCE 实体集合，不再依赖
  未经客户端证明的 kind-8 前导假设。

## 2. 运行时与资源证据

已有只读节点快照记录了原生铸剑谷的成功状态：

```text
scene_challenge_nodes ...
0:k0:id10001 ...
1..4:k3:id4294967295 ...
5:k2:id1000@120,120
6:k1:id20020 ...
7:k1:id20021 ...
8:k1:id30006 ...
```

这证明 `00蓬莱仙岛_02.sce` 中 `(120,120)` 的 kind-3 小猴子最终成为客户端
`nodeKind=2, actorId=1000` 的真实可触碰节点，不是服务端扫描器的离线推断。

同一原生资源的解压 payload 为 483 字节，关键实体顺序是：

```text
offset 322: kind=3, pos=(120,120), id=1000, actor=e_monkey.actor,
            effect=e_ghostfireR.actor
offset 395: kind=3, pos=(12,12), id=3, actor=b_flowers01.actor,
            effect=e_ghostfireR.actor
offset 469: kind=8, pos=(70,38), property 1=38
offset 481: 00 00
```

因此 kind-8 位于两条有效 kind-3 **之后**。它不是“只有放在怪物前方，客户端才进入怪物
解析分支”的通用前导。

`测试地图.sce` 的静态实体区则暴露了真正的首个偏差：

```text
offset 38: entity_count = 6
offset 40..428: 六条已有实体（kind 4,7,8,7,4,5）
offset 428..501: 历史部署追加的 kind=3,id=1001
```

新增 kind-3 被写在六条计数实体之后，但 offset 38 仍然是 6。服务端逐字节扫描能够找到
该记录，客户端按实体集合边界消费时不会把它算作第七条实体。这早于资源更新、screen 重入、
Actor motion descriptor 和最终崩溃位置，是当前复现的第一次数据偏离。

另外两个原生样本否定了固定 marker 顺序：

- `01桃花岛_01.sce`：kind-8 后有 `00 00`，随后是四条 kind-3 毒泥怪；
- `05上古皇陵_02.sce`：四条 kind-3 直接跟在普通场景实体后，末尾没有 kind-8。

原生资源允许的实体布局不止一种；服务端不能从 kind-8 的相对位置臆造统一前导。

## 3. IDA 证据

| binary | function/address | finding |
| --- | --- | --- |
| `江湖OL.CBE` | `ParseActorFullInfoBlob(0x0100F094)` | 入口先读取 `actorRecordCount`，逐条读取 `actorTag,x,y,propertyCount`。 |
| `江湖OL.CBE` | `LoadSceneDataFromStream(0x01006204)` | 先读取全部 MAP 模板，再读 Actor 模板/摆放和尾部 short，然后把当前流位置交给实体 callback。 |
| `江湖OL.CBE` | `0x0100F478..0x0100F5B2` | `actorTag=3/14` 创建场景节点；tag 3 明确写 `nodeKind=2`。 |
| `江湖OL.CBE` | `0x0100F56E/0x0100F592` | field 14 写 actor ID，field 17 绑定 Actor 资源。 |
| `江湖OL.CBE` | `0x0100F70A..0x0100F744` | kind 8 是普通 actorTag 分支，只读取其属性；没有“开始怪物区”的控制流。 |
| `江湖OL.CBE` | `scene_node_find_or_create(0x0100EFC4)` | tag 3 通过正常节点分配入口建立场景节点。 |

`ParseActorFullInfoBlob` 同时证明 kind-3 的五个属性信封与原生字节一致：field 14 ID、
field 15 名称、field 16 视觉提示、field 17 Actor，以及 token kind 3 / field 3 的效果 Actor。

## 4. 成功路径与失败路径

### Success path

1. 客户端从 SCE 实体集合读取 kind-3。
2. `ParseActorFullInfoBlob` 的 tag 3 分支创建 `nodeKind=2` 节点并绑定 ID/Actor。
3. 触碰火团发送正常的 `1/4/1`，服务端按该节点下发 `1/2/2 + 1/4/5`。
4. `mmBattleMstarWqvga.cbm:HandleBattleStartMsg(0x66CC)` 从真实场景节点复制怪物进入战斗。

### Failure path

1. 部署器在 EOF 或臆造的 kind-8 前导后追加 kind-3。
2. 它没有更新目标场景已有的计数实体集合。
3. 服务端宽泛字节扫描错误地把边界外记录视为已部署、已计数、依赖已就绪。
4. 客户端不创建目标 kind-2 节点；后续重入和战斗逻辑均无法补救缺失节点。

## 5. Negative Evidence

- 把 kind-8 移到新增 kind-3 前方并复制同 MAP 的 marker：部署可通过，但客户端仍不出现
  怪物；铸剑谷原生顺序也直接反证该规则。
- screen 重入和资源依赖修正：解决了独立生命周期问题，但不能让实体集合边界外的记录变成
  客户端节点。
- 服务端逐字节 kind-3 扫描：只能证明字节存在，不能证明客户端 parser 消费了该记录。

## 6. 实现与第二个根因

`mock_server_scene_task.c` 现在按以下步骤生成部署资源：

1. 从不可变的服务端基础 SCE 解压 payload；
2. 按 `LoadSceneDataFromStream` 的真实读取顺序跳过所有 MAP 模板、Actor 模板和摆放记录；
3. 在 callback 实体计数所覆盖的 `recordsEnd` 处插入完整 kind-3；
4. 实体计数增一，同时保留 `recordsEnd` 之后的所有原生字节；
5. 重新解析并验证计数、节点数、行内容和压缩往返。

调查还找到了第二个独立根因：旧辅助函数只跳过第一个 MAP 文件名和其中两个 short，
恰好把第一个 MAP 模板末尾常见的 `1,1` 解释成了臆造的“scatter header”。因此单 MAP
模板场景是偶然命中，`c00蓬莱仙岛_03.sce` 这种双 MAP 模板场景必然错位。新前缀解析器
直接重放 IDA 证明的客户端读取语法，不再依赖 `1,1` 特征或逐字节候选搜索。

`09华山_02.sce` 是 196 个资源中唯一不符合该已证前缀的旧式孤例：它在零 Actor 计数后还有
未被当前 IDA 分支解释的脚本/摆放块。生产代码对它显式解析失败，而不是在后续字节中搜索
一个“刚好能解析到 EOF”的数字。在取得该孤例的独立客户端分支证据前，不对它部署新怪物。

## 7. 验证结果

- [x] `测试地图.sce`: count `6 -> 7`，records end `428 -> 501`，原尾部 73 字节不变。
- [x] `00蓬莱仙岛_02.sce`: count `4 -> 5`，原生小猴子仍是第一条 kind-3，尾部 88 字节不变。
- [x] `01桃花岛_01.sce`: count `8 -> 9`，原生四条 kind-3 顺序不变。
- [x] `05上古皇陵_02.sce`: count `7 -> 8`，原生四条 kind-3 顺序不变。
- [x] `c00蓬莱仙岛_03.sce`: 双 MAP 模板前缀定位到 offset 111，count `4 -> 5`。
- [x] 遍历 196 个发布 SCE；除已记录的 `09华山_02.sce` 外，所有非 `b_` 场景均能从真实前缀定位实体列表。
- [x] 重复部署从不可变基础资源产生字节级相同结果，不重复追加。
- [x] `scene-battle-monster-field18-regression`。
- [x] `native-scene-hangup-spawn-regression`。
- [x] `scene-transition-entry-contract-regression`。
- [x] `instance-guide-direct-entry-regression`。
- [ ] `make -j2`: `server_main.o` 编译成功，但 PID 47912 正在运行的 `bin/jh-online-server.exe`
  锁定了输出文件，链接器返回 `Permission denied`。未终止用户进程；使用同一组对象和链接参数
  输出到 `obj/server/jh-online-server-candidate.exe` 已成功，证明没有其他编译/链接错误。
- [x] 使用新服务端发布测试地图，客户端从临安直入后可见火团。
- [ ] 将本体修正为 `e_monkey.actor`、数量设为 5 后重新部署，验收任一火团触碰进入战斗。

## 8. 火团可见但触碰不战斗：本体 Actor 与数量

实体集合修复发布后，用户确认 `测试地图.sce` 已出现测试怪物火团；在火团附近移动时，
服务端只收到正常的 `WT 2/1` 移动包，没有收到场景怪碰撞应产生的 `WT 4/1`。因此首次偏离
已经从服务端战斗响应之前，收敛到客户端场景节点的本体/碰撞契约。

数据库只读记录显示该草稿把 `e_ghostfireR.actor` 同时配置为 `actor_resource` 和
`effect_resource`。原生铸剑谷对照则明确为：

```text
field14 monster_id = 1000
field17 body actor  = e_monkey.actor
field18 child actor = e_ghostfireR.actor
```

`e_ghostfireR.actor` 可以渲染火团，但它是 field18 子特效，不是提供怪物 motion/collision
行为的 field17 本体。故“看得到火团”不等于客户端创建了可触发 `4/1` 的战斗实体。修复在
配置所有权层完成：四种原生 `e_ghostfire*` 退场资源从本体选择器排除，服务端保存和部署也
拒绝它们作为本体；旧错误草稿保持可见但必须显式改选实体 Actor，避免把未知怪物静默猜成
小猴子。当前测试小猴子的本体应选择 `e_monkey.actor`，退场特效继续选择
`e_ghostfireR.actor`。

同次需求新增 `quantity`（`1..5`）。它不是一个 Actor 的视觉复制数，而是一条草稿展开出的
真实 kind-3 节点数。以配置 `(x,y)` 为中心，五个序号确定性映射为：

```text
0: (x,    y)
1: (x-16, y)
2: (x+16, y)
3: (x,    y-16)
4: (x,    y+16)
```

每个展开项都有完整的 field14–18、独立坐标和独立 scene-node ordinal。部署容量、实体计数、
部署账本 `configured_count`、发布源匹配和指纹均按展开后的节点数计算。旧表自动增加
`quantity TINYINT UNSIGNED NOT NULL DEFAULT 1`，因此升级不会把其他旧草稿意外扩成五只；
管理员可把当前测试草稿改为数量 5 后显式重新部署。

新增回归断言包括：退场火团不可作为本体、数量 5 生成五个不同坐标、实体和节点计数均
增加 5、五条记录均保留同一怪物 ID/本体/退场特效，以及从同一基础 SCE 重建两次字节完全
一致。`make -j2`、`scene-battle-monster-field18-regression`、
`native-scene-hangup-spawn-regression`、`scene-transition-entry-contract-regression` 和
`instance-guide-direct-entry-regression` 均通过。最终人工验收边界仍是：将测试草稿本体改为
`e_monkey.actor`、数量改为 5，部署并完整重启客户端后，应看到五个火团，任一碰撞产生
`4/1`，随后既有 `2/2 + 4/5` 分支进入战斗。

## 9. 触碰调度链重新取证（2026-08-23）

用户再次复现后，`scene-battle-collision.log` 证明五个测试节点均已进入客户端活动表：

```text
actor=1001 kind=2 occupied=1 collision=01004ce9
positions=(120,160),(104,160),(136,160),(120,144),(120,176)
```

玩家位置也已移动到 `(104,176)` 等相邻坐标，但没有碰撞回调、候选
`TriggerAutoBattle(0x010183A0)` 入口或 `WT 4/1`。服务端只收到正常 `WT 2/1`
移动请求及空 ACK。因此当前第一次可观察偏离位于客户端场景触碰调度之前，不在服务端
`4/1` handler、战斗响应或 Actor 节点创建阶段。

本轮 IDA 交叉核对推翻了两个过早命名：

| binary | function/address | finding |
| --- | --- | --- |
| `江湖OL.CBE` | `TriggerAutoBattle(0x010183A0)` | 确实扫描 25 个节点并调用 `node+64`，成功时仅输出 actor ID 和节点 index；没有构造或发送网络包，IDA 中也没有静态调用 xref。它是否属于场景触碰链仍待原生运行时证明。 |
| `江湖OL.CBE` | `SendNPCInteractReq(0x01037ED4)` | 构造 `WT 4/1` 的 `id/index/posx/posy`，但唯一静态调用者是 `task_hall_activate_selected_entry(0x010492B0)` 的 action 13。它是任务大厅路径，不能作为场景怪物触碰发送者证据。 |
| `mmGameMstarWqvga.cbm` | `sub_46F6(0x46F6)` | `+108` 调用来自模块私有对象 `unk_2854`，参数是 action 13 等输入状态；不是主 CBE API 表中 `TriggerAutoBattle` 的已证调用。 |
| `mmGameMstarWqvga.cbm` | `sub_604(0x0604)` | 场景 screen logic 会调用主 API 的其他槽位，但当前静态反编译没有直接出现主 API `+108` 调用。 |

当前协议取证记录：

```text
phase: native scene-monster collision dispatch
status: hypothesis

request:
  wt_kind: 4
  wt_subtype: 1
  objects: native sender unresolved
  key_fields: expected id/index/posx/posy; must be confirmed from native packet
  sample_len: unresolved
  packet_log: historical native 4/1 exists; current test scene emits none

response:
  wt_kind: 2 + 4
  wt_subtype: 2 + 5
  objects: existing scene-battle response path

ida_evidence:
  binary: 江湖OL.CBE + mmGameMstarWqvga.cbm
  function: 0x010183A0, 0x01037ED4, 0x010492B0, mmGame 0x0604/0x46F6
  failure_branch: real scene scheduler/sender remains unresolved

runtime_evidence:
  trace_lines: five live kind-2 nodes; no candidate entry/callback/WT 4/1
  client_effect: fireballs visible, touching does not enter battle

negative_evidence:
  missing_or_bad_field: no request exists yet; response fields cannot be causal
  observed_failure: movement reaches monster coordinates without collision dispatch
```

为下一次原生对照，`src/main.c` 的只读探针新增两类记录：

- 每次进入 `0x010183A0` 都记录 LR、四个参数、当前场景、screen、调用模块基址和模块内地址，
  不再要求命中目标 actor 才记录；
- 仅当真实网络发送缓冲区解析为 `WT 4/1` 时，记录 network LR 以及栈中可识别的 CBE/CBM
  返回地址。探针不生成包、不调用回调，也不修改寄存器或客户内存。

下一次必须使用同一新构建依次复现原生 `00蓬莱仙岛_02.sce` 小猴子和测试场景。若原生
路径进入候选函数，可用 LR 锁定真实调度调用者；若原生 `WT 4/1` 出现但候选函数仍未进入，
则正式排除 `0x010183A0`，从上行栈记录反查真正 sender。只有确定两条路径的第一处状态差异
后才允许修改业务行为。

### 9.1 最新复现与 screen logic 地址纠正

最新复现只进入 `测试地图.sce`。五个 `actor=1001` 节点再次以 `kind=2,occupied=1` 和
`collision=0x01004CE9` 创建成功，但整个运行没有出现 `scene_battle_trigger`、碰撞回调、
`scene_battle_uplink` 或 `WT 4/1`。服务端只有进入测试地图后的 `WT 2/1` 移动请求；本轮没有
先进入原生 `00蓬莱仙岛_02.sce`，因此不能把原生成功路径缺失误判为测试节点字段差异。

此前把运行时场景 logic `0x05017137` 映射为 `mmGame:0x70F4` 是基址计算错误；结合整组
screen 回调的相对地址，正确映射是动态装载基址 `0x05016B32` 加模块本地地址：

```text
logic   0x05017136 -> mmGame:0x0604
destroy 0x050171D4 -> mmGame:0x06A2
render  0x050170CE -> mmGame:0x059C
pause   0x05017090 -> mmGame:0x055E
resume  0x05017024 -> mmGame:0x04F2
init    0x05018014 -> mmGame:0x14E2
```

`mmGame:sub_604` 才是场景每帧 logic。IDA 显示它按顺序调用主 API `+1080`、主 API
`+324`、模块私有对象 `+80`、三个延迟释放分支，最后调用主 API `+2480`。其中
`sub_1834/sub_18F2/sub_21DC` 均只负责释放延迟对象或数组，不是碰撞调度者。四个间接函数的
实际地址尚未静态解析，这是当前最早的 unresolved 边界。

为定位该边界，只读探针现在仅在活动 screen logic 的模块本地地址等于 `0x604` 时记录：

- 当前模块 R9、主 API 表和私有对象地址；
- `+1080/+324/+80/+2480` 四个函数指针；
- 每个目标前八次真实入口的 LR、参数、场景和 logic 本地地址。

该探针只读取活动 screen、寄存器和函数表，不修改 PC/LR、寄存器、客户内存或网络数据。
下一次复现后应先把四个指针映射回 `江湖OL.CBE`，再在拥有真实调度契约的函数中比较原生
小猴子与测试怪物路径。

第一次启用该探针后没有产生 `logic-api-map`。启动器已确认直接运行新构建的 `bin/main.exe`，
遗漏来自探针把 `g_vmDlLoadedApps.buffer` 当成 IDA 模块基址并硬筛 `local==0x604`；该字段的
运行时所有权不足以支持这一假设。筛选已改为客户端自身的稳定契约：活动 screen logic 的
模块 API 表 `+108` 必须解析为 `TriggerAutoBattle(0x010183A0)`。只有满足该指纹时才读取并
记录四个间接调用槽，避免依赖未证明的装载偏移。

### 9.2 控制对象边界与本次复现

2026-08-23 12:14 的再次复现证明动态 API 表映射已生效。真实场景 logic
`0x05017136` 读取到：

```text
main API +1080 -> JianghuOL.CBE:0x0100E26E
main API +324  -> JianghuOL.CBE:0x01018BCE
private +80    -> JianghuOL.CBE:0x01005704
main API +2480 -> JianghuOL.CBE:0x0101930E
```

IDA 重新核对 `mmGame:sub_604` 后发现，`+1080` 的参数不是私有对象指针，而是模块内联对象
`R9+0x2DA8`。`JianghuOL.CBE:0x0100E26E` 先调用该对象 `+72` 的函数指针，再按对象状态清理
其内部缓冲。因此真正需要动态映射的是 `R9+0x2DA8+72`；此前只读取
`R9+0x285C` 所指对象的 `+80`，没有覆盖这个回调边界。

同一 IDA 链还显示：

- `mmGame:sub_1444` 通过主 API `+1108` 初始化 `R9+0x2DA8`；
- `mmGame:sub_59FA` 通过主 API `+244` 把地图/场景参数和 `R9+0x2DA8` 一起提交；
- `CheckMapTileCollision(0x01004CE8)` 是 kind-2 节点 `+64` 中已观察到的碰撞函数，但旧探针
  只在未经证明的 `TriggerAutoBattle` 循环地址记录它，没有直接观察该函数的所有真实调用。

本次运行仍只有五个 `actor=1001,kind=2,occupied=1` 节点创建记录，没有
`logic-target-call`、`scene_battle_trigger`、`scene_battle_uplink` 或 `WT 4/1`。当前日志也没有
逐帧角色/最近节点距离，所以尚不能以这次运行证明几何重叠发生在客户端判定坐标中。

下一版只读探针因此收窄到三个未决边界：记录 `R9+0x2DA8+72` 回调和主 API
`+244/+1108` 的实际地址/入口；在场景 logic 中仅当角色坐标变化时记录最近目标距离；直接
记录所有涉及活动 kind-2 节点的 `CheckMapTileCollision` 入口、调用者和返回值。探针不修改
客户内存、寄存器、PC/LR、响应字节或回调时序。下一次原生小猴子与测试怪物对照将据此区分
“场景控制回调未调度节点”和“节点已参与碰撞但 descriptor/形状不匹配”这两类根因。

该只读探针构建后，`make -j2` 完整通过；
`scene-battle-monster-field18-regression`、`native-scene-hangup-spawn-regression`、
`scene-transition-entry-contract-regression` 和 `instance-guide-direct-entry-regression` 均通过。
本阶段仍是 `unresolved`，不把探针构建通过当作碰撞业务已经修复。

### 9.3 最新复现：逐帧触摸链与 action13 边界

2026-08-23 13:11 的最新复现再次确认五个测试节点已安装：`actor=1001`、
`kind=2`、`occupied=1`、资源为 `e_tiger.actor`，坐标为
`(120,160),(104,160),(136,160),(120,144),(120,176)`。角色移动到
`(175,376)` 时，最近节点为 `(175,347)`，距离平方仅 `841`，仍没有
`WT 4/1` 或任何 `scene_battle_uplink`。

IDA 进一步还原了真正的场景帧逻辑：`江湖OL.CBE:0x01016D2E`
(`SceneTickProcessActors`) 在角色移动状态下先调用 `FindNPCByTouchPos(0x01015CCE)`；
该函数只扫描 `occupied && kind==0` 的节点，`kind==2` 会在入口条件处被跳过。
因此此前把所有 kind-2 节点都归入 NPC 触摸链是错误的。`TriggerAutoBattle(0x010183A0)`
确实位于场景对象 vtable `+108`，会扫描 `occupied && kind==2` 并调用节点 `+64`，但
当前运行从未进入该 vtable 槽，不能据此伪造调用或战斗请求。

为锁定原生小猴子与测试节点的第一处分叉，`src/main.c` 新增了窄范围只读探针：
`action13_boundary` 在 `task_hall_activate_selected_entry(0x010492B0)` 和
`SendNPCInteractReq(0x01037ED4)` 入口记录 caller LR、当前选中槽的 `action/value`、
场景节点表及匹配 index。它只在 `CBE_TRACE_SCE_ENTITY_CALLBACK=1` 时写入
`logs/sce-entity-callback.log`，不调用任何业务函数，不修改寄存器、客户内存或网络包。

同时，`scene-battle-collision.log` 的 `logic-position` 记录现在包含场景对象的
`R9+23852` 触摸/战斗回调指针和 `R9+23768` 战斗 gate。这样下一次复现可以区分“节点未被
场景对象注册到回调”与“回调已注册但 `TriggerAutoBattle` 未被调用”，无需把回调指针强行
写入客户端。

当前首个可观察偏离仍在客户端发包之前：测试场景的角色移动没有进入
`TriggerAutoBattle`，也没有 action13 条目或 `SendNPCInteractReq`。下一次必须在同一构建中
先触碰原生 `00蓬莱仙岛_02.sce` 小猴子，再触碰测试 `actor=1001`，对比
`action13_boundary` 与 `scene_battle_collision.log`；只有确认原生进入的真实调用者和
测试路径缺失的第一状态字段后，才允许修改服务端场景/对话响应。

### 9.4 14:05 对照复现：场景完成被内容清单缓存命中卡住

同一构建先完成原生小猴子触碰，再从临安进入测试地图并触碰 `actor=1001`。原生路径产生
`WT 4/1 id=1000,index=5,pos=(120,120)` 并进入战斗；测试路径没有 `WT 4/1`。但本次服务端
日志显示测试地图并未到达可与原生比较的 ready 状态：

```text
mock_npc_instance_enter ... scene=测试地图.sce ... spawn_enemy=1001 response=30/1
mock_update_chunk_complete ... file=测试地图.sce
mock_scene_resource_positioned_enter_deferred ... missing=e_tiger.actor
scene_lifecycle_moveinfo_ack ... ready=0 pending=1
```

同一运行的内容状态为 `release=17/3114 pending=3`。客户端实际下载了
`e_ghostfireR.actor` 和 `测试地图.sce`，但没有请求 `e_tiger.actor`；五个测试火团仍正常显示，
证明 `e_tiger.actor` 已由客户端本地缓存满足。此前 13:11 的碰撞探针日志早于本次运行，不能
用它证明 14:05 的测试场景已经完成生命周期。

第一次偏离是服务端把“文件名出现在失效清单”错误等同于“该客户端必须下载过该文件”。
内容清单只声明可能需要刷新的资源；客户端按需加载时会跳过本地已有文件。真实
`WT6/1` 来自 `scene_runtime_init_and_sync`，它发生在 SCE 和 Actor 队列解析完成之后，因此
这个请求本身就是同场景未请求依赖已缓存命中的证据。旧逻辑仍等待不存在的
`WT18/7(e_tiger.actor)`，永远不发送唯一的 `30/2`，使 session 保持 `pending=1/ready=0`；
战斗触碰不调度只是这个未完成状态的下游表现。

修复只在已经发送场景进入对象、且收到该目标真实 `WT6/1` 的分支生效：按权威 SCE2 计数
实体解析目标场景及 kind-3 的 field17/field18 依赖，把清单中仍 pending 的同场景文件记录为
`WT6/1-scene-runtime-cache-hit`，随后由既有逻辑发送一次 `30/2(no-posinfo)`。实际通过
`WT18/7` 安装的文件仍在最终 chunk 处记录；不清除其他场景资源，也不伪造战斗请求或修改
客户端状态。

### 9.5 14:23 测试地图复现：场景已 ready，但本次碰撞证据未采集

本次服务端运行（`bin/server_out.txt`，最后更新时间 14:23:53）已经完整记录测试地图的场景内容就绪：

```text
mock_scene_runtime_content_ready scene=测试地图.sce cache_hits=1 combat_records=5
mock_scene_resource_positioned_portal_complete completion=resources+30/2-no-posinfo
mock_moveinfo_source ... scene=测试地图.sce
```

这证明本次进入副本的 SCE、5 个 `actor=1001` 实体和场景 ready 生命周期均已完成。服务端随后只收到移动包，
没有 `WT4/1`、`scene_battle_trigger` 或 `scene_battle_uplink`。

但客户端 `bin/multiplayer-data/player-3/logs/scene-battle-collision.log` 的最后更新时间仍为 13:11:10，
`sce-entity-callback.log` 也没有本次运行的文件，原因是启动脚本中的
`CBE_TRACE_SCENE_BATTLE_COLLISION` 和 `CBE_TRACE_SCE_ENTITY_CALLBACK` 默认值均为 `0`。
因此本次“已复现测试地图触碰”只能证明场景 ready 后未产生上行战斗请求，不能证明角色实际进入了客户端的碰撞判定边界。

当前首个未决边界保持不变：需要在同一构建、同一 `player-3` 进程中启用一次性只读探针后重新触碰，
采集角色坐标、最近节点距离、`CheckMapTileCollision` 入口/返回值及真实 `WT4/1` 上行；在取得这些证据前不修改服务端战斗行为。

### 9.6 15:17 探针复现：已进入碰撞范围，但场景回调为空

本次运行使用临时 `player-3` 探针脚本，日志时间为 15:17:45；服务端同一运行的
`server_out.txt` 最后更新时间为 15:17:46。客户端记录了完整的实体和位置链：

```text
scene_battle_node ... actor=1001 ... collision=01004ce9 ... resource=e_tiger.actor
scene_battle_scheduler phase=logic-position player=84,116 nearest_actor=1001 nearest=80,112 distance2=32 touch_callback=00000000 battle_gate=05412354
```

这次角色已经进入最近火团的碰撞距离（`distance2=32`），5 个节点也都处于活动表中；但
场景对象的 `R9+23852` 触碰/战斗回调指针仍为 `0`。日志没有出现 `logic-target-call`、
`control-callback`、`CheckMapTileCollision` 返回记录、`TriggerAutoBattle` 或 `WT4/1`。
服务端同一时间段也只收到 `WT2/1` 移动包。

因此当前首个可观察偏离已经从“玩家是否碰到实体”收敛为“测试场景未建立场景级触碰回调”；
它发生在服务端 `4/1` handler 之前，也不是节点 `+64` 的碰撞函数返回值问题。不能通过服务端
伪造 `WT4/1`、写入客户端回调指针或直接调用战斗函数来掩盖这一缺口。

下一步需要用同一探针对照原生 `00蓬莱仙岛_02.sce` 小猴子运行，记录原生场景在相同位置的
`touch_callback`、`logic-target-call` 和真实 `WT4/1`。只有确认该指针由哪条客户端初始化路径
建立后，才能判断测试地图缺少的是场景/NPC回调初始化对象，还是当前探针读取的字段在该场景中
具有不同语义。

### 9.7 15:21 原生小猴子对照：客户端确实进入真实碰撞战斗链

本次原生复现的探针文件更新时间为 15:21:55，服务端日志更新时间为 15:21:56。日志出现了
测试地图没有出现的完整链路：

```text
scene_battle_trigger phase=entry pc=010183a0 ... sequence=1..36
scene_battle_uplink phase=wt-4-1 sequence=1 len=60 ...
```

服务端同一运行确认该请求为原生小猴子：

```text
mock_challenge_battle_start id=1000 requested=1000 index=5 pos=(120,120)
net_send connect=0 wt=4/1 len=60 source=builtin-challenge-interaction
```

这证明原生场景的客户端确实会调用 `TriggerAutoBattle(0x010183A0)`，构造并上行真实
`WT4/1`；此前把该函数视为“可能与场景触碰无关”的假设已被运行时否定。测试场景在玩家
进入 `(84,116)`、最近节点 `(80,112)`、`distance2=32` 时没有任何 `scene_battle_trigger`
或 `WT4/1`，因此两条路径的首个已确认分叉位于客户端触碰调度/触发入口之前。

本次临时脚本仍配置 `CBE_TRACE_SCE_NODE_ACTOR_ID=1001`，而原生节点 ID 是 `1000`，所以
没有得到原生节点的 `scan-entry` 和 `before/after-callback` 逐节点记录；这些记录不能用旧的
测试节点日志替代。下一次原生对照应将目标 ID 改为 `1000`，仅补采一次节点扫描参数和
碰撞返回值，不修改任何客户端或服务端业务状态。

### 9.8 15:30 原生复现复核：WT4/1 已确认，节点级补采仍未命中

本次 `actor=1000` 专用脚本运行到 15:30:15，服务端同一时间收到：

```text
mock_moveinfo_source pos=(128,89) scene=00蓬莱仙岛_02.sce
mock_challenge_battle_start id=1000 requested=1000 index=5 pos=(120,120)
net_send connect=0 wt=4/1 len=60 source=builtin-challenge-interaction
```

客户端日志同步出现 `scene_battle_uplink phase=wt-4-1`，并在该上行前后出现
`TriggerAutoBattle(0x010183A0)` 入口。因此原生战斗链结论稳定可复现。

但日志中没有新的 `scene_battle_node actor=1000`、`scan-entry actor=1000` 或
`before/after-callback actor=1000` 记录；这说明节点级探针在本次重入/战斗阶段没有捕获到
原生节点创建边界，不能据此推断原生节点字段。当前仍只能确认测试地图与原生地图在
`TriggerAutoBattle -> WT4/1` 调度结果上存在真实分叉；本轮不修改服务端或客户端业务行为。

### 9.9 16:06 原生补采：碰撞节点、返回值与上行已串通

用户使用 `start-player-3-monkey-collision-probe.bat` 重新进入蓬莱铸剑谷并触碰小猴子。
这次日志补齐了 9.8 缺失的节点级证据：

```text
scene_battle_node ... actor=1000 node=054006a4 pos=120,120 ...
scene_battle_collision phase=scan-entry ... nearest_index=5 callback=05412398
scene_battle_collision phase=before-callback ... actor=1000 index=5
scene_battle_collision phase=after-callback ... actor=1000 index=5 ... result=0
scene_battle_collision phase=after-callback ... actor=1000 index=5 ... result=1
scene_battle_uplink phase=wt-4-1 ... data=050003e8
```

服务端同一运行把上行请求解析为：

```text
mock_challenge_battle_start id=1000 requested=1000 index=5
pos=(120,120) target_source=request-live-node
```

因此 `R4` 是 25 项节点表的真实 index，`0x010184D2/0x010184D4` 分别位于节点
`+64` 碰撞函数调用及返回比较处。返回 `0` 时不触发，角色继续移动后返回 `1`，客户端才
输出 actor ID/index 并发送 `WT4/1`。原生节点与测试节点的 `+64` 都是
`CheckMapTileCollision(0x01004CE8)`，所以不能再把问题归因于测试 Actor 没有碰撞函数。

IDA 对 `TriggerAutoBattle(0x010183A0)` 的复核给出完整准入契约：

1. 节点必须是 `occupied != 0 && kind == 2`；
2. 节点 `+64` 回调必须返回非零；
3. `R9+23768` 必须非空，且 UI、冷却、modal、busy 等全局门必须允许；
4. 成功后函数只写出 `node+100` actor ID 和节点 index，网络发送发生在其调用者后续路径。

旧日志把 `R9+23852` 命名为 `touch_callback`，但该字段不在上述函数的准入条件中；其语义
仍是 `unresolved`，不能用“测试地图该值为 0”作为根因。探针后续改用中性名称
`offset23852`。测试地图已知的 `R9+23768=05412354` 非空，因此它也不是当前首个偏离。

两种场景都收到非空 `WT27/11`：测试地图为 `rows=3,dynamic=1`，蓬莱铸剑谷为
`rows=3,dynamic=3`。这排除了“测试地图完全没有 NPC 目录”的宽泛假设；目录行内容是否
参与场景控制对象初始化尚无 parser/运行时证据，暂不据此改包。

当前第一处确定偏离保持为：测试地图角色已进入节点距离范围，但场景帧没有调用
`TriggerAutoBattle`；原生场景持续调用该函数并在碰撞返回 1 后上行。下一次只读补采目标
收窄为活动场景控制对象 `R9(module)+0x2DA8` 的 `+72` wrapper 与 `+48` 实际派发目标，确认
哪个拥有场景帧调度契约的回调在测试地图中未执行。

为避免取证再次造成卡顿，`src/main.c` 现在缓存活动 screen logic，仅在已知 CBE 检查点、
活动 logic 和已解析动态目标上继续读取；每个节点表最多记录两条通用
`TriggerAutoBattle` 入口，动态目标最多四条。两个临时启动脚本也关闭不再需要的
`CBE_TRACE_SCE_ENTITY_CALLBACK`。这些改动只减少只读日志开销，不调用业务函数、不写客户
内存，也不构造协议包。

```text
phase: native scene-monster collision dispatch
status: hypothesis

request:
  wt_kind: 4
  wt_subtype: 1
  objects: id/index/posx/posy verified by live-node server parsing
  key_fields: id=1000,index=5,pos=(120,120)
  sample_len: 60
  packet_log: scene_battle_uplink phase=wt-4-1

ida_evidence:
  binary: 江湖OL.CBE
  function: TriggerAutoBattle(0x010183A0), CheckMapTileCollision(0x01004CE8)
  dispatch_case: occupied kind-2 node scan
  parser_reads: node+64 callback, node+100 actor id, R9+23768 gate
  failure_branch: test scene does not enter TriggerAutoBattle

runtime_evidence:
  trace_lines: actor=1000/index=5 callback result 0 -> 1 -> WT4/1
  handled_source: builtin-challenge-interaction
  queued_event: normal network response path
  client_effect: native monkey enters battle

negative_evidence:
  missing_or_bad_field: test node +64 and R9+23768 are present
  observed_failure: test scene diverges before collision scan entry

unknowns:
  - name: scene-control dispatch owner
    current_value: control object +48/+72 comparison pending
    why_kept: first unobserved boundary before TriggerAutoBattle
```

### 9.10 16:24 对照复现：真实入口来自 action 分发对象

同一份碰撞日志先记录了原生小猴子成功路径，随后记录测试地图失败路径。测试地图的五个
`actor=1001` 节点均创建成功，角色最近移动到 `actor=1001,index=6` 的距离平方 `45`，
但该阶段没有 `scene_battle_trigger`、碰撞回调或 `WT4/1`。原生阶段则持续进入
`TriggerAutoBattle`，碰撞回调从 `0` 变为 `1` 后立即上行。因此首次偏离仍然位于
`TriggerAutoBattle` 之前，不在几何距离、节点 `+64` 或服务端响应。

运行时两次装载的场景 logic 均精确对应 `mmGameMstarWqvga.cbm:sub_604`：

```text
native: logic=0502f4c6, module_code_base=0502eec2, local=0x604
test:   logic=05017136, module_code_base=05016b32, local=0x604
```

player-3 私有资源与仓库基准资源的 SHA-256 也完全相同：
`429b1f4f07a6702c7609a389f19ee995f35f0f9d6b9c3812c9fb24832a8bf700`。
这排除了“测试进程加载了另一份 CBM 或 sub_604 布局不同”的假设。

原生 `TriggerAutoBattle` 日志的返回地址 `0502f98b` 去 Thumb 位后，相对模块基址正好是
`0xAC8`。IDA 显示该地址是 `mmGame:sub_8A8` 中 `0xAC4: BL sub_68E` 的返回点；
`sub_8A8` 仅在 action `2/3/4` 等分支调用 `sub_68E`。`sub_68E(0x68E)` 随后读取的是：

```text
main callback:    *(R9+0x2054) + 68
private object:   *(R9+0x2850)
private callback: private object + 20
private context:  R9+0x2D44
```

这与上一轮跟踪的 `R9+0x285C -> +80` 是不同对象。后者属于 `sub_604` 每帧 logic 的另一条
调用，不能证明触碰 action 的私有回调已经安装或执行。当前最早未决契约因此修正为：测试
场景是否收到与原生相同的 action，以及 `R9+0x2850 -> +20` 是否指向并执行相同回调。

下一版只读探针仅在动态模块本地 `0x8A8`、`0xAC4` 和 `0x68E` 三个地址低频记录参数和
函数指针；每次场景模块重载后重置小额度计数。它不调用函数、不写客户内存/寄存器、
不改变输入队列和网络数据。只有比较出这三个边界的第一处差异后，才继续追查对应的初始化
数据来源；本阶段不修改场景或战斗业务行为。

### 9.11 16:33/16:34 复现：action 探针基址缺陷与注册契约

用户分别复现测试地图和原生小猴子后，新日志再次确认：

```text
test: actor=1001,index=3 distance2=180; no TriggerAutoBattle; no WT4/1
native: TriggerAutoBattle caller LR=module_base+0xAC8;
        CheckMapTileCollision result=1; WT4/1 emitted
```

启动器确实执行 16:30 构建的 `bin/main.exe`，且该二进制包含 `scene_battle_action` 字符串，
所以不是旧程序。但两次运行均没有该新记录。代码审计确认这是探针缺陷：它仅在活动 logic
已经被识别为模块 `sub_604` 后才建立模块基址；原生触碰阶段活动 logic 已切换到同模块另一
回调，导致虽然 LR 明确是 `base+0xAC8`，探针仍不知道 `base`，从而漏掉此前的 `0xAC4`。
测试场景虽建立了 `sub_604` 基址，移动期间仍未命中 `sub_8A8`，但在原生对照也被漏记的
情况下，尚不能把这条阴性证据单独提升为根因。

IDA 对模块初始化 `sub_1444` 给出了注册所有权：

```text
mmGame:0x149C  main API +52   <- sub_8A8 input callback
mmGame:0x14B8  main API +1100 <- sub_68E touch callback
mmGame:0x14C4  main API +1104 <- sub_66E sibling callback
```

screen 函数表的 init 入口是模块本地 `0x14E2`，因此可在 screen 激活时从 init 指针直接计算
模块代码基址，不需要等待某一种 logic。修正后的探针使用该只读指纹，并在 action 命中时
读取当时真实模块 R9；下一次对照将验证注册后的 `sub_8A8 -> 0xAC4 -> sub_68E` 三个边界。
在获得该对照前，业务根因状态保持 `unresolved`。

### 9.12 16:38 复现：screen init 不是唯一模块基址指纹

修订探针只输出两条 `scene_battle_action`，但地址校验立即否定了它们：记录 PC
`0x0503024A` 减去探针推定基址 `0x0502F9A2` 等于 `0x8A8`，然而同一原生运行已经由
`sub_604` 和 `TriggerAutoBattle` 返回地址证明真实 mmGame 基址是 `0x0502EEC2`；该 PC 的
真实本地地址是 `0x1388`。读取到的 `private_object=0x10` 也与 `sub_68E` 必须解引用对象
`+20` 的契约矛盾。因此这两条是探针误匹配，不能作为原生或测试 action 证据。

根因是 mmGame 内包含多种 screen。场景主 screen 的 init 本地地址是 `0x14E2`，原生触碰
阶段切换后的另一 screen init 本地地址不是 `0x14E2`；不能用任意活动 screen 的 init 减
固定偏移推导模块基址。该推断已删除。

新的基址证据只接受两种已证明来源：测试场景实际执行的 `sub_604`，或原生
`TriggerAutoBattle` 返回地址 `base+0xAC8`。后一种还必须同时匹配：

```text
base+0x604: 10 B5 84 4C 01 21 4C 44
base+0x8A8: FE B5 02 1C EB 48 0C 1C
```

只有双指令指纹通过后才跟踪 `sub_8A8/0xAC4/sub_68E`。本轮业务状态仍为
`unresolved`，没有依据错误探针结果修改回调注册或场景逻辑。

### 9.13 17:12 对照复现：测试怪可成功，分叉收敛到首次场景激活

最新两轮测试地图与原生小猴子对照改变了此前“测试地图从不进入
`TriggerAutoBattle`”的结论。`scene-battle-collision.log` 中已经出现一条完整的测试怪
成功链：

```text
actor=1001,index=6
TriggerAutoBattle(0x010183A0)
CheckMapTileCollision result=1
WT4/1 len=60
```

服务端同一运行把它解析为真实活动节点，而不是默认目标：

```text
mock_challenge_battle_start id=1001 requested=1001 index=6
pos=(120,144) target_source=request-live-node
net_send ... wt=4/1 source=builtin-challenge-interaction
```

因此 kind-3 字段、五个节点、本体 Actor、节点碰撞函数、`WT4/1` detector 和战斗响应均已
至少在同一测试资源上走通，不能继续把它们作为“永远触碰无效”的根因。

这次成功不是随机包处理。服务端日志显示成功前用户在同一测试场景执行过“脱离卡死”：

```text
mock_settings_unstuck_16_2 ... response=16/2-direct-enter
mock_scene_npc_rearm ... next=WT6/1
mock_scene_runtime_direct_enter_object_stream ... response=16/3
... actor=1001 -> WT4/1
```

相邻的 fresh-load 测试运行使用相同 overlay、相同 `actor=1001` 节点和相同角色；角色到最近
节点距离平方 `1`，但没有进入 `TriggerAutoBattle`。随后 fresh-load 的原生铸剑谷立即走通
`actor=1000 -> result=1 -> WT4/1`。数据库只读比较还证明两场景虽有不同 `WT27/11`
目录（测试地图为 1 条服务行加 2 条 SCE 行，铸剑谷为 3 条服务行），但测试地图在目录不变
的“脱离卡死”重入后能够成功，所以该目录差异不是当前首个因果偏离。

当前可检验的根因陈述是：动态测试场景首次启动/资源安装后的 screen 已解析出 kind-3
节点，却没有保持可调用 `sub_8A8 -> sub_68E -> TriggerAutoBattle` 的 action 注册状态；
`16/2 -> 16/3` 的正常同场景重入重新执行 mmGame 初始化后恢复该状态。最早待确认的状态边界
是 `mmGame:sub_1444` 对主 API `+52 <- sub_8A8` 的注册是否在 fresh-load 完成后被后续
bootstrap 覆盖或解除，而不是 `WT4/1` 之后的服务端行为。

探针下一版只记录主 API `+52` 的真实函数入口、传入 callback、LR 和活动 scene。目标地址
直接从已验证 mmGame 主 API 表读取，不再从任意 screen init 猜模块基址；记录最多 12 次，
不做逐帧节点扫描之外的新高频操作，也不修改输入、客户内存、寄存器或协议数据。

### 9.14 已被否定的启动 SCE 重入假设

以下记录的是当时基于间接证据作出的假设，已经被后续真实客户端崩溃序列否定；保留它只为
说明为何曾加入启动 `30/1`，不能再将其作为协议契约或修复依据。

```text
NPC 副本引导              -> WT30/1(scene,posinfo)
客户端发现测试地图.sce 缺失 -> WT18/7(final install)
旧服务端收到首次 WT6/1     -> resources + WT30/2(no posinfo)
```

`江湖OL.CBE:parse_scene_response_entry(0x010396D6)` 对 `30/1` 读取 `scene + posinfo`
并经场景对象 `+116` 再次进入场景；`parse_scene_posinfo_field(0x01039770)` 对
`30/2(result=0, no posinfo)` 只执行 `ResetDownloadState`，不会重新进入 mmGame 场景。
因此旧路径能留下已解析的 kind-2 测试怪节点和可见火团，却没有完成资源安装后所需的
mmGame input/action 注册。相同资源、节点和目录在“脱离卡死”走 `16/2 -> 16/3` 的正常
同场景重入后，客户端自然走通 `TriggerAutoBattle -> CheckMapTileCollision -> WT4/1`，构成
该状态差异的运行时反证。

首次修复仍有两个过早的假设：真实客户端在目标 SCE 安装后可先发组合的
`WT25/5 + 6/*` runtime-sync，而不是独立 `WT6/1`；同一目标的特效 Actor 可以在 SCE 后
完成 `WT18/7`，从而覆盖旧的全局“最近完成文件名”。实际日志恰好是 `测试地图.sce` 和
`e_ghostfireR.actor` 已完成、`e_tiger.actor` 仍处于 manifest pending。旧代码把这个 Actor
视为必须继续下载的资源，保持场景 pending，客户端因此永远到不了下一次 `WT6/1` 和场景
重入。

修复把“目标 SCE 是否在副本直入后真实安装”改为每客户端、每 manifest 文件的
`installGeneration`。只有最终 `WT18/7` 清除对应 pending 位时该代次才递增；runtime-sync
识别出的缓存 Actor/特效只清除 pending 位，绝不递增代次。直接 NPC 副本 `30/1` 记录当时
目标 SCE 的代次，首次后续的独立 `WT6/1` 或组合 `WT25/5 + 6/*` 仅在以下条件同时满足时
返回一次新的 `WT30/1(scene,posinfo)`：

1. 目标来自 NPC 副本引导的直接 `30/1`；
2. 该目标先前尚未重入；
3. 目标 SCE 的 final-`WT18/7` 安装代次已高于进入时快照。

组合 runtime-sync 只会在目标 SCE 已不再 pending 后，将该 SCE 的权威 kind-3 记录所引用、
仍 pending 的 Actor/特效视为客户端缓存命中。随后才检查上述 scoped re-entry 条件。这样
`测试地图.sce -> e_ghostfireR.actor` 的完成顺序不会丢失 SCE 安装事实，且仍拒绝把未下载的
SCE 当作安装完成。客户端重新发出的下一次真实 runtime-sync 才保留原有
`27/11 + resources + WT30/2(no posinfo)` 完成链。未发生 SCE 下载的副本、蓬莱普通场景、
传送石和与目标不同的 Actor/特效安装都不满足条件，继续使用原有路径。修复只构造已由
parser 证明的网络对象；没有调用客户端回调、写客户内存/寄存器或伪造 `WT4/1`。

本次用户复现还排除了“仅副本直入”这一范围假设。原始服务端日志显示角色选择后直接进入
`测试地图.sce`：SCE 和 `e_ghostfireR.actor` 都完成了 `WT18/7`，但首个 `WT6/1` 仍由
`mock_scene_startup_followup_complete ... action=no-second-scene-enter` 收尾，因此没有命中
副本 target 的重入条件，也没有出现 `WT4/1`。同一客户端的碰撞取证中，测试怪 kind-3 节点
和战斗触发槽均已存在，但失败帧的 `touch_callback=00000000`；而通过正常场景重入的测试怪
会取得非空 callback 并上行 `WT4/1`。首个偏离仍是资源安装后的场景生命周期，而非上行
detector 或战斗响应。

启动场景现在复用同一份 per-client SCE 安装事实，但条件进一步限定为：角色选择创建的首
场景仍 pending、当前场景精确等于活动角色的持久化场景、位置有效，且该精确 SCE 在本次
协商后 final-`WT18/7` 的安装代次非零。它同时支持独立 `WT6/1` 与组合 runtime-sync：先按
权威 kind-3 资源清除缓存命中的 Actor/特效 pending 位，再返回一次原生 `30/1(scene,posinfo)`；
新的场景 target 随后的 runtime-sync 仍只返回一次 `30/2(no posinfo)`。未下载的首场景和
任何非角色选择路径仍保留原有 `30/2` 收尾。

2026-08-23 验证：

- `make -j2` 通过。
- `scene-transition-entry-contract-regression` 重新编译并通过：普通缺失 SCE 在最终
  `WT18/7` 后仍只有一次 `30/2(no posinfo)`；无下载副本保持原完成路径；副本目标 SCE
  最终安装并随后完成另一个 Actor 的真实组合 runtime-sync 时，首次只有一次 `30/1`，
  第二次只有一次 `30/2(no posinfo)`，第三次不再有 `30/*` 场景对象；角色选择直达场景
  在 SCE 与后续 Actor 完成后也遵循相同的一次 `30/1`、一次 `30/2` 顺序。
- `instance-guide-direct-entry-regression` 重新编译并通过。
- `scene-battle-monster-field18-regression` 重新编译并通过；196 个发布 SCE 的实体集合
  解析和测试地图 kind-3 注入保持不变。

### 9.15 真实崩溃反证与撤回（2026-08-23）

真实运行的服务端序列是：

```text
WT18/7(测试地图.sce final install)
-> WT6/1
-> mock_startup_sce_install_reenter 返回 30/1(scene,posinfo)
-> 客户端 WT2/3
-> WT25/5
-> parse_actor_motion_descriptor(0x0100DA4E) 崩溃
```

`30/1` 的 parser `江湖OL.CBE:parse_scene_response_entry(0x010396D6)` 会经场景对象 `+116`
再次进入场景。角色选择的 actorinfo 已创建第一个场景壳，第二个启动 `30/1` 与它重叠；
`AllocActorSceneNode(0x010177DA)` 在 25 个槽位均不可用时返回零，随后
`parse_actor_motion_descriptor(0x0100DA4E)` 执行 `STRB R0,[R2,R1]`。崩溃现场
`R2=0x13B,R1=0` 与该空节点写完全一致。故不能以客户端空指针保护修复。

服务端已撤回启动路径的 `30/1` 构造及其独立、组合 runtime follow-up 入口。角色选择启动的
首个 follow-up 继续使用既有 `mock_scene_startup_followup_complete`，只把已 final-`WT18/7`
安装的 SCE 所引用资源记录为缓存就绪，不构造任何新的位置型场景对象；即使客户端随后发出
`WT2/3 -> WT25/5`，回归只允许无位置 ACK 或请求对象，禁止重新进入场景。直接 NPC 副本的
独立 `30/1` 契约未改动。

这项改动只消除了已确认的崩溃回归。测试怪首次进入时缺少触碰 action 注册的原因仍为
`unresolved`；`16/2 -> 16/3` 是已证明的特定业务契约，不能塞进启动 `WT6/1` 作为替代。

本轮验证：`make -j2` 通过；重新编译并运行
`scene-transition-entry-contract-regression` 通过，覆盖 SCE 安装后的启动 follow-up、
`WT2/3` 和 `WT25/5` 均不得产生位置型场景进入；重新编译并运行
`scene-battle-monster-field18-regression` 通过，输出测试地图实体数 `6 -> 7`、战斗节点数
`0 -> 1`，并通过 196 个发布 SCE 的计数实体解析。

### 9.16 无崩溃复现后的 action 注册取证

本轮角色选择直入 `测试地图.sce` 已无 `0x0100DA4E` 崩溃。服务端记录该 SCE final-`WT18/7`
安装、`mock_scene_runtime_content_ready combat_records=5`、
`mock_scene_startup_followup_complete action=no-second-scene-enter`，随后只收到五条 `WT2/1`
移动包，未收到 `WT4/1`。因此阻断点仍在客户端上行前。

IDA 重新确认 `mmGameMstarWqvga.cbm:sub_1444(0x1444)` 是输入 action 的所有者：它经主 API
`+52` 注册 `sub_8A8`；`sub_8A8(0x8A8)` 对 action `2/3/4` 调用 `sub_68E(0x68E)`；后者才调用
主 CBE 的碰撞扫描入口。启动器默认关闭 `CBE_TRACE_SCENE_BATTLE_COLLISION`，本次客户端日志
没有新的 action 注册或碰撞 callback 记录，不能据此修改服务端响应。

新增 `bin/multiplayer/start-player-3-scene-battle-probe.bat` 只设置两项现有只读探针：
`CBE_TRACE_SCE_ENTITY_CALLBACK=1` 和 `CBE_TRACE_SCENE_BATTLE_COLLISION=1`。下一次同样从
角色选择进入测试地图并触碰时，须比对 `scene_battle_input_registry api+52`、
`scene_battle_scheduler logic-api-map`、`scene_battle_trigger` 和 `WT4/1`；该脚本不改输入队列、
客户内存、寄存器或服务端协议。

### 9.17 首场景内容失效与下一轮取证（2026-08-23）

19:59 的无崩溃复现再次收到最终 `WT18/7(测试地图.sce)`，随后服务端只看到五条
`WT2/1`，没有 `WT4/1`。运行后检查证明客户端 `bin/JHOnlineData/测试地图.sce` 已是 891
字节，且 SHA-256 与当前 `jh_online` overlay 一致；因此发布字节和安装结果不是本次的首个
偏离。

但是该安装发生在角色选择的 `actorinfo` 已经创建首个 scene shell 之后。`30/1(scene,posinfo)`
重入会重叠分配节点并已被真实 `0x0100DA4E` 崩溃否定；`30/2(no-posinfo)` 只执行下载状态收尾，
不调用 mmGame 的场景进入接口。相反，设置“脱离卡死”的真实 `16/2 -> 16/3(result=2)`链由
`mmGameMstarWqvga.cbm:sub_11CE(0x11CE) -> sub_BCC(0x0BCC) -> main API +116` 进入场景，并在
相同测试怪上恢复过 `sub_8A8 -> sub_68E -> TriggerAutoBattle -> WT4/1`。

因此不能把 `30/1`、`16/2` 或 `16/3` 作为对启动 `WT6/1` 的猜测性补包。`player-3` 常规启动器
现默认开启 `CBE_TRACE_SCENE_BATTLE_COLLISION=1`（可由环境变量显式关闭），下次按原角色选择
进入测试地图并触碰即可采集 `main API +52` 中已注册 callback、action `2/3/4` 分发和碰撞扫描的
首个缺口；该开关仅做有上限的只读日志。

### 9.18 20:19 复现：任务假设排除，追踪注册首边界

本轮从角色选择直入 `测试地图.sce` 的服务端记录已经包含
`mock_task_list tasknum=1 persisted_active=1`、非空 `WT27/11` NPC 目录和五条
`combat_records`；客户端也创建了五个 `actor=1001,kind=2,occupied=1` 的
`e_tiger.actor` 节点。玩家多次进入最近节点的距离平方 `1` 或 `16`，但日志仍没有
`TriggerAutoBattle(0x010183A0)` 或 `WT4/1`。

因此“蓬莱铸剑谷能触发是因为挑战小猴子任务，而测试地图没有任务”已被此运行否定：测试
地图同样具有活动任务，且历史上它曾在相同 actor 和 SCE 数据下成功触发战斗。`WT4/1` 的
客户端生成链只使用碰撞成功后的 live actor ID 和节点 index，不读取任务 ID。

IDA 确认 `江湖OL.CBE:SetMapCtrlField6172(0x0101807E)` 是主 API `+52` 的 setter，写入
场景输入回调；`mmGame:sub_1444(0x1444)` 用它注册 `sub_8A8`，后者仅在 action `2/3/4`
分支经 `sub_68E` 调用 `TriggerAutoBattle`。此前探针只能在 scene logic 已运行后才得到 setter
地址，可能错过首次 screen 生命周期中的注册。现将其改为在 `vmAddedScreen` 变化时从
`Global_R9+0x2054` 主 API 表预先读取，并只记录 setter 入口的 callback、上一次 callback、
调用者和 callback 所属 CBM。该操作不写 guest 内存、寄存器、输入或协议数据。

下一次同一启动路径的复现将判定以下互斥结果：未见 `sub_8A8` 注册，见注册后被非预期 callback
覆盖，或注册完成但 action `2/3/4` 未进入。只有该最早差异得到运行时证据，才修改其真正拥有
的启动/场景契约。

### 9.19 20:34 注册已确认，任务与注册假设均排除

新构建的首场景日志已命中主 API `+52` setter：

```text
callback=05017479
lr=0501806f
```

将 callback 去 Thumb 位减去 `mmGame:sub_8A8` 的本地偏移 `0x8A8`，得到基址
`0x05016BD1`；同一基址加 `sub_1444` 中 setter 调用后的返回偏移 `0x149E` 恰为记录的 LR。
并且该基址的 `sub_604`、`sub_8A8` 指令指纹与 IDA 一致。这证明测试图在首次启动也确实执行
`sub_1444 -> API+52 <- sub_8A8`，不是任务缺失，也不是输入 callback 未注册。

此前 trace 只有在后续 scene logic 已知基址时才追踪 `sub_8A8`，所以错过了这个先于 logic 的
注册。探针现以已验证 callback 推导并校验模块基址，然后只读追踪 `sub_8A8`、`sub_68E` 与
`TriggerAutoBattle`。下一次正常触碰可以把剩余分叉精确定位为：输入 action 未到达 callback，
action 值不属于 `2/3/4`，或 callback 到达而其下游未执行。业务修复在取得这一首个差异前保持
未决，不能以重发 `30/1` 或伪造 `WT4/1` 掩盖问题。

### 9.20 追踪基址修正（2026-08-23 21:55）

9.19 的 registration 记录已用 `sub_604` 和 `sub_8A8` 双指令指纹确认 callback 所在模块基址为
`0x05016bd0`。但后续场景 logic 的运行时入口 `0x05017136 - 0x604` 被另一个仅用于 logic
映射的推断值覆盖为 `0x05016b32`。两者相差 `0x9e`，所以旧追踪虽然允许了已注册 callback 的
入口，却用错误基址比较 `sub_8A8`、`sub_8A8+0xAC4` 和 `sub_68E`，导致 action 记录全部漏失。

宿主侧只读探针现将两个概念分离：

- `sceneModuleCodeBase` 仍保留给 TriggerAutoBattle/scene logic 的既有调用链映射。
- `inputDispatchModuleBase` 仅在 API `+52` 收到 callback 且双指令指纹通过时赋值，并独立用于
  三个 input/action 地址的过滤、比较和日志。

该修正不写客户内存、寄存器、PC/LR、输入队列或协议数据。`make -j2` 已通过；
`scene-battle-monster-field18-regression.exe` 通过，仍覆盖 196 个发布 SCE 的实体集合和测试图
五个 kind-3 战斗节点展开。当前没有运行中的客户端或服务端，因而还没有将旧日志误认作此修正
后的 action 结果。下一次同一测试图触碰必须检查 `scene_battle_action` 是否出现，以及 action
是否为 `2/3/4`、是否继续到 `touch-route-before-call` 和 `touch-callback-entry`。

为区分 setter 之后被覆盖与 callback 未获调度，探针还会在场景 logic 中只在值改变时记录
`Global_R9+0x5d28` 的实时输入 callback，以及 `sub_68E` 将调用的主 API `+68` 目标。两个值都
是客户端已有状态的只读快照；它们没有被回写或用于影响事件分发。

### 9.21 主输入分发边界取证（2026-08-23 23:11）

用户随后复现仍不能触发战斗。新的宿主输入记录证明测试地图不是丢失了
`VM_EVENT_KEYBOARD`：方向键按下和释放均以原有路径调用当前 scene screen：

```text
screen=01053438 entry=05017137
event type=0/1, key-mask=00010000/00020000/00008000/00040000
```

地址归一化曾出现一次算术误判，现已排除：经已验证的 mmGame 装载基址
`0x05016BD0` 计算，`0x05017137 - 0x05016BD0 = 0x567`，所以该 entry 是
`mmGameMstarWqvga.cbm:sub_566(0x566)+1`，不是 `sub_742` 的中间地址。
IDA 显示 `sub_566(a1,eventType,eventArg)` 的 ABI 与宿主实际传参一致；对键盘按下
`eventType=0`，它读取 `*eventArg` 并调用主 API `+88`。因此不能把 screen logic
entry 当成错误入口，更不能在宿主侧直接调用 `sub_8A8` 或 `TriggerAutoBattle`。

截至该复现已获得的链路是：

```text
硬件键盘队列 -> sub_566 -> sub_540 -> main API +88
                                      ? -> sub_8A8 -> sub_68E -> TriggerAutoBattle
```

`sub_8A8` 已由 `sub_1444 -> main API +52` 正常注册，且运行期间
`R9+0x5D28` 保持该 callback；但最新日志尚未记录主 API `+88` 的实际函数入口，所以第一处
未证明的边界正是这个间接分发器。`src/main.c` 增加了最多 24 条只读
`scene_battle_input_dispatch phase=entry` 记录：在 scene logic 中读取 `mainApi+88` 的真实目标，
仅当客户机自然执行到该 PC 时记录 R0-R3、LR 和 `R9+0x5D28` callback。它不改变任何客户机
内存、寄存器、PC/LR、输入事件、网络请求或响应。

下一次同一路径复现的判定为：

1. 未出现 `input_dispatch`：`sub_540` 的间接调用目标/时序与当前 API 表不一致；
2. 出现 `input_dispatch` 而未到 `sub_8A8`：在该 CBE 分发器中继续以 R0 键码和状态门为准逆向；
3. 到达 `sub_8A8` 但 action 非 `2/3/4`：定位键码到 action 的映射契约；
4. 到达 `sub_68E` 仍无 `TriggerAutoBattle`：检查 `main API +68` 的 callback 所属与返回路径。

本轮没有修改场景包、场景进入包或战斗包，也没有恢复已确认会导致
`parse_actor_motion_descriptor(0x0100DA4E)` 崩溃的启动 `30/1` 重入。

验证：`make -j2` 通过；`obj/server/scene-battle-monster-field18-regression.exe` 通过。
当前环境未设置 `CBE_AUTOMATION_MYSQL_PASSWORD`，因此无法在不触碰用户 `jh_online` 数据库的前提下
启动隔离端到端客户端自动化；没有把静态回归误作客户端战斗已验收。

### 9.22 启动资源失效与原生场景重建入口（2026-08-24）

重查本次失败运行的原始服务端序列后，蓬莱与测试地图的可检验差异已收敛到资源生命周期，而非
挑战小猴子任务：

```text
WT18/9 manifest(测试地图.sce, e_ghostfireR.actor, e_tiger.actor)
-> 角色选择建立首个测试场景壳
-> WT18/7(测试地图.sce final install)
-> WT18/7(e_ghostfireR.actor final install)
-> WT6/1(scene-runtime-init)
-> mock_scene_startup_followup_complete(action=no-second-scene-enter)
-> 仅 WT2/1，未有 WT4/1
```

蓬莱铸剑谷的资源在首场景创建前已可用；测试地图在角色选择后才被启动内容清单失效并由
`WT18/7` 安装。相同测试地图经过用户主动的“脱离卡死”流程后，`16/2 -> 16/3(result=2)`
可正常重建并恢复 `sub_8A8 -> sub_68E -> TriggerAutoBattle -> WT4/1`。因此任务、SCE
实体、碰撞函数和战斗响应均不是首次偏离。

IDA 复核了两个不能混淆的客户端契约：

1. `JianghuOL.CBE:SceneTickUpdatePositions(0x010163A4)` 是主 API `+88`。方向键传入的是
   原始 bitmask；它按 `R9+0x5C82` 控制状态调用 `R9+0x5D24` 的控制委托，不能据
   `R9+0x5D28=sub_8A8` 已注册就推断该 raw-key 路径应直接进入战斗 action。
2. `mmGame:sub_11CE(0x11CE)` 是注册到主 API `+48` 的响应对象扫描器。仅当网络响应对象
   本身为 `16/3` 且 `result=2` 时，它调用 `sub_BCC(0x0BCC)`；后者读取 `scene`、`posinfo`
   和 `exitid`，调用主 API `+116` 建立场景，随后经 `+68(0)` 清除场景模式。这个入口正是
   已成功的同场景重建所使用的客户端路径。

此前的 `30/1(scene,posinfo)` 启动重入已经在真实客户端触发
`parse_actor_motion_descriptor(0x0100DA4E)` 的节点容量崩溃，不能重新启用。也不能仅因
`16/3` 在其它业务请求中有效，就猜测性地把它塞进 `WT6/1` 响应。

`src/main.c` 的下一次只读运行证据会在已验证的 mmGame 模块基址上记录：

```text
scene_battle_lifecycle phase=response-dispatch      # sub_11CE
scene_battle_lifecycle phase=scene-enter-object     # sub_BCC
```

记录包含自然到达的寄存器、LR 和主 API `+116` 目标。判定规则为：若 `WT6/1` 的现有响应已经
触发 `sub_11CE`，则对象流具备模块扫描的接收边界；若没有 `sub_BCC`，缺失的是合法场景进入对象
而非输入/碰撞数据。只有同时取得该运行时证据和相同 header/object 组合的 parser 证据后，才可在
拥有 `WT6/1` 响应契约的服务端层补充精确对象；否则保持 `unresolved`。

本轮仅增加有上限的只读 tracing。`make -j2` 与
`obj/server/scene-battle-monster-field18-regression.exe` 均通过；没有修改服务器响应、客户机
内存、寄存器、PC/LR 或场景资源字节。

### 9.23 首场景 SCE 安装后的原生重建修复（2026-08-24）

新的用户复现取得了 9.22 规定的判定证据：正常 `WT6/1` 响应流自然进入
`mmGameMstarWqvga.cbm:sub_11CE(0x11CE)`，但未进入它唯一的场景对象分支
`sub_BCC(0x0BCC)`。该响应没有 `16/3(result=2)`；因此客户端保留了由角色选择
actorinfo 创建、但在 SCE 安装前初始化的场景壳。测试怪实体、碰撞数据、`sub_8A8` 注册和
任务均在该壳中存在，却没有通过 `main API +116` 完成原生场景重建，最终没有 `WT4/1`。

蓬莱铸剑谷不是因“挑战小猴子”任务才可战斗：它的首场景资源在 actorinfo 建壳前已可用，不会
经历这个失效-再安装的生命周期。测试地图则经历：

```text
WT18/9(测试地图.sce, Actor/effect manifest)
-> 角色选择 actorinfo 创建首场景壳
-> WT18/7(测试地图.sce final install)
-> WT6/1 完成业务/资源 follow-up
-> standalone WT25/5
```

修复在服务端保存每账号、每会话的精确 startup-SCE 状态，只有同时满足以下条件才处理该
独立 `WT25/5`：

1. 角色选择首场景 follow-up 已启动，活动角色与当前场景名称、当前网格坐标一致；
2. 当前场景 SCE 在本轮 manifest 中实际经 final `WT18/7` 安装，安装代次非零且未变化；
3. 首个 startup follow-up 已将同一 scene/position 标为 completed；
4. 事件在 arm 后 90 scheduler tick 内，且该一次性标记尚未消费。

匹配时 `vm_net_mock_build_startup_sce_install_scene_enter_response()` 只返回一个已由 IDA 和
运行时共同证明的对象：`16/3 { result: typed-u8(2), scene, posinfo, exitid }`。正常网络响应
事件随后令客户端自然执行：

```text
sub_11CE -> sub_BCC -> main API +116 -> scene_runtime_init_and_sync
```

handler 随即清除一次性标记，并 re-arm 现有 `WT6/1` NPC runtime catalog；第二次 `WT25/5`
继续回到原有 scene-default ACK。它不处理组合 `WT25/5 + 6/*`，不处理蓬莱、传送石、战斗关闭、
普通 refresh 或未安装 SCE。启动路径没有恢复 `30/1(scene,posinfo)`，所以不会重新触发已取证的
`parse_actor_motion_descriptor(0x0100DA4E)` 场景节点耗尽崩溃。

修改点：`mock_server_equipment_npc.c` 负责会话隔离状态；
`mock_server_interaction_login.c` 在已验证的 startup SCE runtime-ready 边界 arm；
`mock_server_scene_task.c` 负责精确 `16/3` 对象与 parser 对应字段；
`mock_server_dispatch.c` 在通用 `WT25/5` ACK 前选择该 handler。

验证结果：

- `make -j2` 通过。
- 重新编译并运行 `obj/server/scene-transition-entry-contract-regression.exe` 通过；它验证 final
  `WT18/7` 不发第二个 `30/1`、首个独立 `WT25/5` 恰发一个 `16/3(result=2)`、第二个独立
  `WT25/5` 不重复发场景进入对象。
- 重新编译并运行 `obj/server/scene-battle-monster-field18-regression.exe` 通过；196 个发布 SCE
  和测试地图五个 kind-3 战斗节点的解析结果未变化。

本机没有 `CBE_AUTOMATION_MYSQL_PASSWORD`，未启动隔离端到端客户端自动化，亦未连接或写入用户的
`jh_online` 数据库。人工复测应从角色选择进入测试地图，服务端应记录
`builtin-startup-sce-install-scene-enter` 与
`mock_startup_sce_install_scene_enter ... response=16/3-result2`；客户端应继续产生 runtime-sync，
随后触碰测试怪应出现正常 `WT4/1`。

### 9.24 触碰后的 `0x01004EA8` 空视觉上下文（2026-08-24）

最新复现已不再是碰撞未触发：测试火团产生了真实
`WT4/1 { id=1001, index=5, pos=(136,160) }`，服务端保持该 live tuple 并返回
`WT2/2 + WT4/5`。客户端随后在首个战斗绘制帧于
`JianghuOL.CBE:DrawMapTileLayer(0x01004EA8)` 解引用空的视觉上下文 `R0+0x0C`。

第一次违约在本体 Actor 的资源所有权，不在任务或碰撞逻辑：测试 SCE 的 field17 是
`e_tiger.actor`，该文件存在于服务端 `web/fs/JHOnlineData`，但不在客户端
`bin/JHOnlineData`。field18 的 `e_ghostfireR.actor` 已下载，所以火团仍可见；这不能证明
field17 已经由 DreamFactory 解析。服务端又在 `WT6/1` 将未下载的 body Actor 标记为
`scene-runtime-cache-hit`，掩盖了该缺失。

蓬莱铸剑谷正常不是因为“挑战小猴子”任务。它的 kind-3 使用已存在于客户端资源目录的
`e_monkey.actor`，并同样以 `e_ghostfireR.actor` 作为 field18。任务只影响任务状态，不参与
`WT4/1` 的 live index 或 `WT4/5` 的场景节点复制。

修复包括两点：

1. 客户端 DreamFactory 的裸 `*.actor`/`*.gif` 查找现在解析到
   `JHOnlineData/<name>`；本地不存在时经既有、真实的 `WT18/7` 请求下载，再将实际缓存路径
   记录到资源表。
2. 服务端 `WT6/1` runtime-sync 仅可证明 SCE 已运行，不再把 kind-3 field17 body Actor
   伪记为安装完成。field17 的安装只以实际 `WT18/7` 完成或客户端资源查找结果为准。

新增 `scripts/scene-battle-actor-cache-regression.c` 覆盖裸 Actor/GIF 名称边界，以及
`e_monkey.actor -> JHOnlineData/e_monkey.actor` 的客户端缓存解析。该回归不启动 VM、窗口、服务端
或数据库；完整战斗验收仍需在隔离客户端路径中确认 `WT18/7(e_tiger.actor)`、`WT4/5` parser 和
首帧战斗 UI。

### 9.25 `e_tiger.actor` 名称占位掩盖缓存缺失（2026-08-24）

后续崩溃复现保留了客户端实际收到的 46-byte `mmorpg_updatetemp` 清单。它按长度前缀完整包含
三项：`测试地图.sce`、`e_ghostfireR.actor`、`e_tiger.actor`。因此第三项不是 manifest 编码、
分块截断或数据库排序问题。启动清单的语义是失效本地文件；之后仍由实际资源访问触发 `WT18/7`，
不会保证按清单顺序预下载所有项。

本轮首次违约在 DreamFactory 宿主资源缓存：`vm_resource_cache_note_package()` 可以先登记 Actor
名称而没有本地文件路径。旧 `vm_resource_cache_lookup_id()` 把这种 name-only 记录直接当作命中；
`vm_DF_GetResourceByFileName()` 也先接受同名 package entry。测试图 field17 的
`e_tiger.actor` 因而没有进入 `JHOnlineData/e_tiger.actor` 的缺失资源路径，服务端也没有收到
对应 `WT18/7`。field18 火团可单独下载和显示，但 field17 的 visual context 保持空，直到
`WT4/5` 复制场景节点并在 `DrawMapTileLayer(0x01004EA8)` 解引用 `+0x0C` 才崩溃。

修复使两条资源入口一致：无有效本地路径的 cache entry 不再返回资源 ID；对裸
`*.actor`/`*.gif`，若 `JHOnlineData/<leaf>` 缺失，DreamFactory 会先走既有的本地缓存缺失资源
传输并在成功后加载，再允许 package 查找。它不伪造战斗、修改客户内存或跳过 parser。新增回归还
断言 name-only `e_missing_body.actor` 不得作为已安装资源返回 ID，并覆盖非空但文件已被
manifest 失效删除的陈旧路径；两者都必须重新走缺失资源传输。待人工复测的正向证据是
`WT18/7(e_tiger.actor)`、最终安装日志，以及随后正常的 `WT4/5` 和首帧战斗画面。

### 9.26 Package 名称 ID 绕过本地 Actor 缓存（2026-08-24）

9.25 的缓存修复后，用户仍复现同一 `JianghuOL.CBE:0x01004EA8` 空上下文崩溃。该复现提供了
反证：`bin/JHOnlineData/e_tiger.actor` 仍不存在，服务端只收到 SCE 与火团的 `WT18/7`；但是场景
解析日志的 `node+248` 已非零并被记录为 `resource=e_tiger.actor`。该字段实际是 CBE node 保存的
名称字符串指针，不能代表 Actor bytes 已被加载或 visual context 已建立。因此不得继续依据这个
日志字段推断 Actor 已安装。

最早违约位于 DreamFactory 的两个 Host API：旧
`vm_DF_GetResourceIDByFileName()` 先调用 `DataPackage_GetFileID()`，只有 package ID 不存在时才查询
本地缓存。测试 SCE 的外部 body Actor 名称在 package 中存在，因此先返回 package 的名称 ID，完全
跳过了 9.25 的缺失文件路径；后续 `vm_DF_GetResourceByResourceID()` 又优先用该 package ID 取名称
引用。碰撞可正常发送 `WT4/1`，服务端也正常回复 `WT2/2 + WT4/5`，但 battle parser 没有实际的
Actor visual context，首帧绘制仍以 `R0=0` 访问 `+0x0C`。

修复把裸 `*.actor`/`*.gif` 的所有权放在这条真实 ID 链的最前端：它先调用缓存解析，必要时按既有
`WT18/7` 传输下载并返回保留的本地 resource ID；按 ID 取资源时，保留 ID 也先回到本地缓存，不能
重新落回 package 名称引用。package 中的非 Actor 数据及未安装时的正常错误路径不变。没有修改碰撞、
战斗请求/响应、客户机内存、寄存器、PC/LR 或 CBE 指令。

`scripts/scene-battle-actor-cache-regression.c` 现以最小 Unicorn 客户机内存直接调用
`vm_DF_GetResourceIDByFileName("e_monkey.actor")`，断言得到的是本地缓存 ID 且 Host API 将同一值写回
`R0`，并保留 name-only 与陈旧路径断言。`make -j2` 和该回归均通过。待人工路径验收的首个正向证据
必须是 `WT18/7(e_tiger.actor)`；其后才检查 `WT4/5` parser 和战斗首帧。

### 9.27 直接 DataPackage 虚表绕过 Actor 缓存（2026-08-24）

9.26 的外层 `DF_GetResource*` 修复后，用户仍在触碰测试火团时复现同一崩溃：
`DrawMapTileLayer(0x01004EA8)` 以空 `R0+0x0C` 绘制战斗单位。最新
`bin/server_out.txt` 给出首个可观察偏离：客户端只对 `测试地图.sce` 和
`e_ghostfireR.actor` 完成 `WT18/7`，服务端随后明确记录
`scene-task-subset-followup missing=e_tiger.actor`，但客户端仍可发送真实的
`WT4/1`，服务端随即返回 `WT2/2 + WT4/5`。

根因不是挑战小猴子任务，也不是 `WT4/5` 字段。`main.c` 的 DreamFactory
虚表分派表明，CBE 场景 Actor 解析还会直接调用
`DF_DataPackage_GetFileID`、`DF_DataPackage_GetFile` 和
`DF_DataPackage_GetFileByID`。这些入口此前仍先把 package 中的同名记录当成
资源本体，完全绕开 9.26 修复过的 `DF_GetResource*` 包装函数。该记录只有
`e_tiger.actor` 的名称而没有客户端文件字节，因此战斗单位继承空视觉上下文；
火团 field18 的独立下载无法弥补 field17 body Actor。

修复下沉到 `src/vmFunc.c` 的三条实际虚表路径：裸 `*.actor`/`*.gif` 名称先通过
客户端 `JHOnlineData` 缓存解析，缺失时走既有同步 `WT18/7` 资源传输；成功后返回
保留的本地 resource ID。`DataPackage_GetFileByID` 和
`DataPackage_GetFileNameByID` 也优先识别此 ID，不能再落回 package 名称占位。
资源无法取得时返回正常的未找到结果，不再伪造 package 命中。没有改写客户内存、寄存器、
CBE 指令、碰撞或战斗协议。

验证：`make -j2` 通过；重新编译并运行
`obj/client/scene-battle-actor-cache-regression.exe` 通过。回归覆盖外层 API 和直接
`DataPackage` 的 ID 查询、按 ID 读文件、按名称读文件、ID 反查名称四条路径；
`obj/server/scene-battle-monster-field18-regression.exe` 也通过。端到端人工验收仍需从
干净的测试客户端缓存进入测试地图，确认 `e_tiger.actor` 及其 Actor 描述符引用的 GIF 都有
`WT18/7` 完成记录，随后触碰生成 `WT4/1 -> WT4/5` 且首帧战斗 UI 不再进入
`DrawMapTileLayer(0x01004EA8)` 空上下文。
