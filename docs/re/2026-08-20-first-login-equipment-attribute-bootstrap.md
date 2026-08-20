# 首次登录装备属性种子时序

Date: 2026-08-20

Status: corrected lifecycle implementation; deterministic packet regression passed; player-3 runtime root cause confirmed; battle-return recomputation observed; normal login-time refresh contract still under investigation

## 1. 当前卡点

- 可见现象：角色首次登录进入场景后，打开“玩家信息 -> 属性”只显示
  ActorInfo 的基础值；已经穿戴的装备不计入属性页。
- 触发方式：选择一个已有至少一件耐久大于零的装备的角色，首次进入场景，
  不进行穿脱操作，直接打开属性页。
- 本轮最小目标：在现有 `30/21` 背包网格和 `7/7 type=2` 已穿戴装备实例完成
  item-manager bootstrap 后，经客户端确认的正常协议/生命周期重新计算场景属性；不得
  将装备值写入 ActorInfo 的基础属性字段，也不得直接调用或写入客户端状态。

## 2. 运行时证据

`bin/server_out.txt` 的角色 `10036` 首次登录记录如下：

```text
mock_title_role_select ... response=1/1/6+1/1/15
net_send ... wt=1/6 ... resp=455
mock_backpack_grid ... kind=30 subtype=21 gridnum=36
mock_equipment_login ... rows=8 ... response=7/7-type2
net_send ... wt=5/10 ... source=builtin-group-type1 resp=1956
```

这套原有顺序是正确契约：`30/21` 和 `7/7 type=2` 属于首个组同步，而不是
选角应答的一部分。

## 3. IDA 证据

| binary | function/address | findings |
| --- | --- | --- |
| `江湖OL.CBE` | `scene_runtime_init_and_sync(0x01012FB4)` | 处理场景 ActorInfo 后构造 `5/10` 与三条 `7/7 type=1..3` 请求，并在 `0x0101363E` 调用 `scene_rebuild_status_meter_node(1)`。响应仅能通过普通 event 7 在函数退出后投递。 |
| `江湖OL.CBE` | `scene_rebuild_status_meter_node(0x0100FED8)` | 遍历已穿戴实例，耐久大于零时从 `equip.dsh` 与实例强化字段累加属性。 |
| `mmGameMstarWqvga.cbm` | `sub_D04(0x00000D04)` | `7/7 type=2` 解码 `seq,itemId,currentDurability,commonEquipmentAttributes`，再调用主道具管理器的安装路径。 |
| `江湖OL.CBE` | `SendEquipUseReq(0x01032B8A)` | `type=2` 以 `-1` 为第三参数复制完整装备到 category 15，不从背包删除源行。 |
| `mmTitleMstarWqvga.cbm` | `role_manage_screen_handle_network(0x53EC)` | 选角成功的 `1/1/6` 交给 `mmorpg_LoginRecord`，`1/1/15` 才进入标题的下一阶段；这不是 mmGame item-manager 的同步回调。 |

## 4. 调用链与首次偏离

1. 原有首登路径中，选角应答只发送 `1/1/6 ActorInfo`、`1/1/15`；随后真实客户端
   发送 `5/10 + 7/7(type=1)`。
2. `src/server/mock_server_social.c` 的对应 builder 已在该回复中调用
   `vm_net_mock_append_backpack_role_grid_main_objects()`，按 `30/21 -> 7/7(type=2)`
   安装主道具管理器与穿戴实例。
3. 为提前修复属性刷新而把这套 helper 加到选角回复后，helper 在过早的 title
   回调里消耗了 `g_netMockBackpackGridSeededRoleId`。最新 player-3 日志因此是
   `1/6 resp=2177` 带装备对象，但后续 `5/10 resp=234` 没有 `30/21` 和
   `7/7 type=2`。

首个错误状态是一次性装备种子被放入不拥有 item-manager 回调的选角响应、导致真正
的组同步为空；不是 ActorInfo、装备目录、耐久或数值公式错误。

## 5. 拟定响应契约

选角请求保持原有窄匹配 `1/1/6`，成功时仍只回复：

```text
1/1/6  ActorInfo
1/1/15 title continuation
```

随后的窄匹配 `5/10 + 7/7(type=1)` 回复在组信息和 type-1 状态对象之后，使用既有
helper 发送：

```text
1/30/21 backpack grid              (仅非空网格)
1/7/11 reservoir counts            (仅有 802/803 行)
1/7/7  type=2 equipped instances   (仅非空已穿戴集合)
```

这时 helper 才把 `g_netMockBackpackGridSeededRoleId` 标为活动角色；重复组请求不重播，
重新选角及商城返回的既有 re-arm 逻辑会重新开启这一次性种子。

## 6. 排除方案

- 不把装备加成混入 ActorInfo：后续穿脱会调用
  `scene_rebuild_status_meter_node()`，将基础字段和已穿戴实例重复相加。
- 不伪造 `7/8` 穿脱成功响应：该分支要求真实的选中物品和操作 pending 上下文，
  不是登录状态同步契约。
- 不直接写场景/属性内存或调用客户端重算函数：这些会绕过客户端协议生命周期。

## 7. 验证计划

- 确认选角响应的对象顺序严格为 `1/1/6 -> 1/1/15`，不会消耗装备种子。
- 确认同一角色紧随其后的第一个 `5/10 + 7/7(type=1)` 恰有一次 `30/21` 与
  `7/7 type=2`，其后的重复组请求不重播。
- 确认重选同一角色会重新发送一次种子。
- 捕获一次“登录 -> 属性页 -> 战斗结束 -> 属性页”的完整只读 trace，记录战斗结束后
  `scene_rebuild_status_meter_node` 的调用返回地址、输入对象和装备链状态；只有找到其可复现的
  普通登录协议契约后，才调整服务端响应。
- 覆盖无背包行、无已穿戴装备、耐久为零和强化装备的既有编码边界。
- 执行 `make -j2`。隔离自动化尚缺
  `CBE_AUTOMATION_MYSQL_PASSWORD`，无法在本轮启动带数据库的端到端客户端夹具。

## 8. 实现结果

撤回 `src/server/mock_server_interaction_login.c` 中把
`vm_net_mock_append_backpack_role_grid_main_objects()` 注入 `1/1/6` 与 `1/1/15` 之间
的代码。既有的 `src/server/mock_server_social.c` 组同步 builder 已在正确的
`5/10 + 7/7(type=1)` request contract 下拥有该 helper，因此不改变任何客户端逻辑或
ActorInfo 数据来源。

新增 `scripts/first-login-equipment-attribute-bootstrap-regression.c`。它在内存中创建
一个具有普通背包行和耐久装备的角色，不启动监听器且不连接 MySQL，直接调用真实选角
和组同步 builder，断言：

1. 选角对象严格为 `1/1/6 -> 1/1/15`，且没有消耗种子；
2. 第一个组同步回复按顺序含一次 `30/21 -> 7/7(type=2) -> 7/7(type=3)`，其 type-2
   `iteminfo` 含一条已穿戴耐久实例，type-3 的 `iteminfo` 精确为零行字节 `00`；
3. 同一生命周期的重复组同步不会再次追加该三个对象；
4. 再次选角后，第一个组同步重新发出一次完整种子。

该夹具断言 type-3 终结对象为默认协议：它紧邻 type-2 装备对象，且 `iteminfo` 精确为一个
零行计数字节 `00`。重复组同步仍不得重放这三个对象。

2026-08-20 执行的隔离命令为：

```powershell
gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 -ffunction-sections -fdata-sections -w scripts/first-login-equipment-attribute-bootstrap-regression.c obj/server/gifDecode.o obj/server/mystd.o obj/server/mysql-client.o obj/server/md5.o '-Wl,--gc-sections' -o tmp/first-login-equipment-attribute-bootstrap-regression.exe -lpthread -liconv -lm -lkernel32 -lws2_32
.\tmp\first-login-equipment-attribute-bootstrap-regression.exe
```

2026-08-20 修正后重新执行，输出
`first-login equipment attribute bootstrap regression passed type3_completion=1`。其运行记录证明：两次选角
回复均保持 `1/1/6+1/1/15`；每次**首个**真实 `5/10 + 7/7(type=1)` builder 都输出一次
`mock_backpack_grid` 和一次 `mock_equipment_login`；两者之间的重复组请求没有重播种子。
夹具只构造本地内存角色和会话，不监听端口、不连接 MySQL；输出中的可选战斗效果/经验卡
状态读取报错仅来自该刻意无数据库夹具，未影响被断言的响应字节。

`make -j2` 已重新编译 `src/server_main.c` 并成功链接
`bin/jh-online-server.exe`。客户端运行时验收仍未完成。当前工作区没有
`CBE_AUTOMATION_MYSQL_PASSWORD`，不能启动需要 MySQL 的端到端客户端夹具。若客户端仍
显示裸装，下一步应保留原始包和 `mock_title_role_select` 对象顺序日志，继续核对标题到
场景的事件边界，不改动 ActorInfo 属性来源。

### 部署核对（2026-08-20）

早期 player-3 重测中 `response=1/1/6+1/1/15`、`resp=455` 且二进制也只含旧日志
字符串，说明当时尚未运行注入版服务端。

随后 player-3 的新日志已经显示注入版 `response=1/1/6+backpack/equipment-seed+1/1/15`、
`resp=2177`，但 `5/10 resp=234`。这复现并定位了早注入抢占种子的根因。修正后新的
`bin/jh-online-server.exe` 应恢复选角 `resp=455`，并在紧随其后的 `5/10` 日志中依次显示
`mock_backpack_grid`、`mock_equipment_login`；不能只以选角包变大作为通过依据。

### 第二次 player-3 复现与运行时根因（2026-08-20）

本次已确认使用修正后的服务端：

```text
mock_title_role_select ... response=1/1/6+1/1/15
net_send ... wt=1/6 ... resp=455
mock_backpack_grid ... gridnum=36
mock_equipment_login ... rows=8 ... response=7/7-type2
net_send ... wt=5/10 ... resp=1956
```

用户仍在战斗前看到裸装属性。这排除了缺少组请求、服务端二进制陈旧、以及没有发送
装备形状对象三种假设。

本轮的 `login-equipment-forensics.log` 提供了更直接的真实客户端证据，修正了此前对
`type=2` 的保守判断：

1. tick 126 首次进入 `scene_rebuild_status_meter_node(0x0100FED8)` 时，
   `R9+0x6048` 的装备链表头为零；
2. 紧随其后的 tick 127，`SendEquipUseReq(0x01032B8A)` 以第三参数 `-1` 连续进入 8 次；
3. tick 150 第一次属性页入口已经从 `R9+0x6048` 读到 8 条完整的 category-15 装备实例，
   包含 item ID、序号、耐久、原始类别和强化等级；
4. 在这些 type-2 安装与属性页之间，没有第二次 `scene_rebuild_status_meter_node` 入口。

因此 `7/7 type=2` 确实完成了登录装备安装；首个被违反的契约是**场景首次重算先于
异步的组同步装备安装完成，安装后没有正常的重算触发**。属性页本身仅展示现有场景状态，
不会补做该重算。用户报告的“一场战斗后才正确”说明战斗结束路径存在合法重算，但其具体
请求/响应与回调顺序尚未取证，不能据此伪造穿脱成功包或直接重算客户端内存。

调查阶段曾通过一次性只读追踪记录 `sub_D04/type=2`、
`scene_rebuild_status_meter_node`、属性页入口与 category-15 装备列表。它证明了这里的时序，
但已经连同其环境变量和 player-3 启动器改动一并删除；正常客户端不再写入该追踪日志。

### 第三次 player-3 复现：战斗回场重算（2026-08-20）

用户完成一场战斗后，同一份只读追踪记录到了完整的时序：

1. tick 139 首次 `scene_rebuild_status_meter_node(0x0100FED8)` 仍在装备链为空时执行；
2. tick 140 的 8 次 `SendEquipUseReq(type=2)` 随后安装了全部穿戴实例；
3. tick 188 属性页已经能读到全部 8 条实例，但仍没有重算入口；
4. tick 362 再次进入 `scene_rebuild_status_meter_node`，此时同一条装备链已经完整，
   因而客户端正常把装备加成应用到场景属性。

服务端同一轮的原始流程包含正常的战斗结算 `4/7 + 7/11`，随后客户端回到场景；这不是
登录期可以伪造的刷新包。现有 battle parser 将 `4/7` 解释为活动战斗上下文中的结算和
生命/法力变化，脱离战斗发送会破坏协议所有权，故明确排除。

本轮新增的 LR 记录把调用者精确缩小为 `mmBattleMstarWqvga.cbm`：

```text
scene-rebuild-entry tick=296 lr=05031CE3
mmBattle code_base=0502EF40
local return=0x2DA2
```

本地 `0x2DA2` 是 `BattleSettle_UpdateCharAttrs(0x2C50)` 中紧随 vtable `+276` 调用的
返回位置；该 vtable 正是 `scene_rebuild_status_meter_node(1)`。在此之前，`4/7`
的 `HandleBattleSettleMsg(0x743C)` 已把结算 `hp/mp` 写入 battle-local 增量，
`BattleSettle_UpdateCharAttrs` 再把它们合并到角色并重算装备属性。因此该调用既不是
`0x01017FA0` 的等级成长，也不是通用场景刷新；它要求存活的 BattleScreen、结算缓存和
`4/7` parser 生命周期。

这条链路同时排除了将 `4/7`、第二次场景进入、穿脱响应或战斗退出动作复用于登录。当前
客户端在登录 `7/7 type=2` 安装完成后没有再发送可由服务端回答的状态刷新请求；现有
`7/7 type=2/3` 独立查询分别只消费 `7/20` 与 `7/32`，而 `7/8` 成功分支严格依赖用户
实际操作产生的 pending item。后续只能从原始服务登录包或未恢复的登录前装备载入字段
继续取证，不能以伪造业务状态替代该契约。

### 已安装 mmGame 模块复核（2026-08-20）

这次直接反汇编了 `bin/JHOnlineData/mmGameMstarWqvga.cbm`，而非只依赖旧导出。其
`sub_D04` 的 `0x1050..0x10A8` 分支读取 `type` 后只有两条有效路径：

```text
type == 1 -> item-manager +0x34 的普通新增/叠加操作
type == 2 -> item-manager +0x68，第三参数固定为 -1 的装备安装操作
other     -> 跳过操作；共同尾部只设置 item notification 状态
```

`sub_11CE` 仅在 WT 对象为 `1/7/7` 时调用此分支。`type=2` 的装备安装路径随后汇合到
共同通知尾部，**自身**不调用 `scene_rebuild_status_meter_node`；重复 type-2 行仍只会造成
重复插入。这里最初把“其它 type 不进入 item-manager”误写成“其它 type 不可能触发重算”。
后续对共同尾部的完整反汇编发现 `type=3` 有一个独立的终结分支，详见下文“CBM 子包重算
回调穷举”。因此，`7/7 type=3` 是待运行时验证的协议候选，不能在没有副作用验证前直接
作为登录修复部署。

### 事件时序与基线复核（2026-08-20）

`scene_runtime_init_and_sync()` 在同一次 guest 调用中先依次发送 `5/10` 与三条
`7/7(type=1..3)`，随后在 `0x0101363E` 调用
`scene_rebuild_status_meter_node(1)`。模拟器的正常网络路径则是：

```text
guest send -> TCP worker -> scheduler_tick()
           -> drain completion -> queue event 7 -> guest callback
```

因此首个重算必然早于这次组同步的 event-7 回调；在发送路径中改为重入执行 callback
虽然可能改变表面结果，却会破坏现有网络事件边界，不能作为服务端协议修复。这个时序也
不能通过把装备对象拆成更多 event-7 包而改变。

同时，直接复核 `scene_rebuild_status_meter_node(0x0100FED8)` 证明它从角色的
ActorInfo 基线取初始状态，再对每件有效装备调用
`AddActorStatBonus(0x0100FE2C)`；后者对生命、法力和各属性槽均为直接加法。故将
装备总值提前写入 ActorInfo 只能让首屏表面正确，并会在战斗结算或真实穿脱后的下一次
重算重复相加，仍不是可接受的修复。

### 可复核原始包导出（2026-08-20）

调查期间曾使用一次性、只读的抓包程序取得首登窗口内实际使用的对象字段与顺序。该程序和
专用启动入口已在协议验证后移除；以下原始样本保留为取证记录：

```text
logs/login-equipment-packet-capture/run-*/
  manifest.tsv       # 收发方向、scheduler tick、墙钟、连接、event、sequence、对象摘要
  001-uplink.wt      # 原始 1/1/6
  002-downlink.wt    # 服务端对该请求的原始回复
  ...
```

上行对象头为五字节 WT 形式，下行为六字节对象头；样本目录中的 `README.txt` 说明该差异。
抓包仅用于本次取证，不属于当前客户端或服务端的运行功能。

### player-3 原始包复现（2026-08-20 18:06）

用户在调查期间完成“选角 -> 进入场景 -> 打开属性”的未战斗复现。证据目录为
`bin/multiplayer-data/player-3/logs/login-equipment-packet-capture/`
中的 `run-00015983-00046708-001`；`manifest.tsv`、原始 `.wt` 与同次
`login-equipment-forensics.log` 可交叉复核。关键时序如下：

```text
tick 139  uplink  1/1/6                         # 选角
tick 141  downlink 1/1/6 + 1/1/15                # title 成功 shell
tick 143  uplink  5/10 + 7/7(type=1)
tick 143  uplink  7/7(type=2)
tick 143  uplink  7/7(type=3)
tick 143  scene_rebuild_status_meter_node        # 装备表头仍为 0
tick 144  downlink 5/10 + 10/26 + 30/21 + 7/11
                  + 7/7(type=2,iteminfo=8 rows) + 7/20 + 7/32
tick 144  downlink 7/20                          # 独立 type=2 状态查询
tick 144  downlink 7/32                          # 独立 type=3 状态查询
tick 144  8 x SendEquipUseReq(..., -1)           # 客户端安装八件装备
tick 519  属性页读取完整的 8 条 category-15 装备实例
```

服务端同一轮日志把首个组合请求归为 `builtin-group-type1`（回复 1983 bytes），并将两条
独立 `7/7` 请求归为 `builtin-game-type`（分别回复 34/36 bytes）。这确认装备对象位于
组合登录回复并非误路由；`7/7(type=2/3)` 独立查询仍然只消费 `7/20/7/32`，不能挪作
装备列表包。

因此本次抓包排除了“遗漏了装备行、遗漏 7/20/7/32 或对象被送到错误请求”三个假设，
也直接复现了最早偏离：正常 event-7 回调在首次场景重算之后才安装装备，而客户端直到战斗
结算才有另一个已知重算入口。当前抓到的是本模拟服务的完整实际线包；若要恢复不同的原服
行为，仍需要原服首登下行包或已确认的、在首次重算前载入装备的客户端状态来源，不能以
未请求的业务成功包替代。

### 客户端重算入口穷举（2026-08-20）

对 `江湖OL.CBE` 中所有**直接**调用
`scene_rebuild_status_meter_node(0x0100FED8)` 的 BL 指令重新逐一复核，结果如下。
这不是只按函数名猜测，而是以被调地址、紧邻的状态写入和本轮 `player-3` 包/PC 追踪
交叉确认。

| 调用点 | 前置状态 | 是否可作为首登修复 |
| --- | --- | --- |
| `0x0101363E` | `scene_runtime_init_and_sync()`；连续发送三条 `7/7` 后立即执行 | 否；正是本问题中早于异步回调的首次重算。 |
| `0x01017FA0` | 先把等级倍率乘入 HP/MP/属性/经验等角色槽，再重算 | 否；伪造这条路径会额外应用等级变更。 |
| `0x0101CDC0` | 倒计时/持续效果状态递减、归零处理后重算 | 否；会篡改持续效果时钟和角色状态。 |
| `0x010335EA`、`0x010335F0`、`0x010337C2` | `HandleItemOperationResponse(0x01033544)`；持有装备操作的 pending 行、序号和成功结果，先完成替换/安装/卸载 | 否；登录装备同步没有 pending 穿脱请求，伪造成功回包会破坏物品管理器生命周期。 |
| `0x01011B2E` | `1/7/13` 分支：读取字段 `result`、`flag`、`type`，仅在 `result=1 && type=2 && flag=1` 时先调用全局状态回调再重算 | 未确认；本轮原始包没有 `1/7/13`，服务端也没有该对象 builder，不能把它当作无副作用刷新通知。 |

`1/7/13` 是唯一仍可在网络对象处理阶段到达的直接入口。其组别并非猜测：网络事件
分发器 `0x01012E4C` 在 `r3==7` 的常规数据事件中按对象组别分派；组别 `7` 的表项唯一
跳到 `0x01011C88`，后者再以 subtype `13` 进入该分支。它明确读取
`result`/`flag`/`type`，并且重算前还有一项全局状态回调；同一常量区还含有
`itemname`、`bookinfo`、`finish`、`expinfo` 以及 GBK“修炼完成”等业务字符串。这证明它
高度疑似修炼结果对象，而不是已经确认的“空刷新”对象。未取得该对象的原始请求/响应与
UI 后果前，向首登组合回复追加 `1/7/13` 会伪造修炼完成，违反协议取证边界，不能作为
修复。

截至此节的 CBE 主包枚举中，没有确认一个可安全用于首登的直接入口：等级、计时、穿脱、
战斗和 `1/7/13` 都带有不满足的业务前置。之后对已安装 `mmGame` 子包的完整尾部反汇编
发现了一个此前遗漏的 `1/7/7 type=3` 终结分支。它不是直接调用、网络重入或伪造穿脱；
但其服务器业务语义与登录期副作用尚未验证，故在独立运行时测试完成前仍不能把它当作
正式修复。直接回调、重入 network event、虚构升级/计时/穿脱成功对象依旧明确排除。

### CBM 子包重算回调穷举（2026-08-20）

本节只以当前安装的 `bin/JHOnlineData/*.cbm` 的 Mstar 指令为依据；本轮 player-3 的
运行时 loader/返回地址也确认其实际使用的是 `mmGameMstarWqvga.cbm` 与
`mmBattleMstarWqvga.cbm`。QCIF 文件的 Thumb 指令按字节对反序存储，不能沿用 Mstar
偏移或反汇编结果；它需要单独按其字节序复核，且不影响当前 player-3 结论。

对所有子包中与战斗调用相同的共享回调表序列进行筛选：先从共享根取 `+0x101`，再取
函数槽 `+0x14`，以参数 `1` 调用。战斗结算已由运行时 LR 证明该槽就是
`scene_rebuild_status_meter_node(0x0100FED8)`。结果为：

| 模块 | 本地调用点 | 前置与结论 |
| --- | --- | --- |
| `mmGameMstarWqvga.cbm` | `0x1182` | `1/7/7` 的 `sub_D04` 共同尾部；仅 `type=3` 到达。候选，需验证。 |
| `mmGameMstarWqvga.cbm` | `0x4958` | `1/7/13` 成功分支；仍是修炼/专用业务结果，排除。 |
| `mmBattleMstarWqvga.cbm` | `0x2D9C`、`0x6DAE` | 战斗结算/战斗状态处理；依赖 BattleScreen 和战斗局部状态，排除。 |
| `mmShopMstarWqvga.cbm`、`mmTitleMstarWqvga.cbm` | 未发现该精确共享表序列 | 没有可在登录装备同步后调用同一重算槽的模块路径。 |

`mmGame:sub_D04` 的关键指令顺序为：

```text
0x105E..0x1074  type == 2 -> item-manager +0x68(..., -1)  # 安装装备
0x1094..0x10B2  对每个 7/7 对象设置共同 notification/wait 状态并通知 UI
0x1168..0x116C  仅 type == 3 才进入终结分支
0x117A..0x1186  写终结标记 -> shared +0x101/+0x14(1)
0x1188..0x1190  清除共同 notification/wait 状态
```

该 `type=3` 分支没有读取 pending 穿脱行、BattleScreen、等级变更或持续效果，也没有检查
前一对象必须是 `type=1`。因而若把**零行** `1/7/7 { type=3, iteminfo=0 }` 放在同一组合
回复中八行 `type=2` 装备对象之后，客户端会先安装八件装备，再走此回调；从 parser 的
零行循环与对象顺序看这是一个可检验的时序候选。独立发出的 `7/7(type=3)` 请求仍只对应
`7/32`，绝不能把那个请求的答复替换为该终结对象。

`type=3` 会写模块状态 `root+0x570+5=1`，并清除共同 notification/wait 状态；因此部署前
必须验证其真实 UI 后果。根因仍是“装备安装晚于首次场景重算”；`type=3` 是唯一能在同一
异步安装回包末尾抵达正确重算回调的终结对象。

验证阶段先出现过一次未命中隔离服务的反例：组合回复仍为 1983 bytes，只有 type-2 装备
对象。随后以实际发送 type-3 对象的原始包、服务端记录和客户端属性页共同确认本节契约。
所有抓包、只读追踪、环境开关和专用启动器均已在验证完成后删除，不参与正常运行。

### 验证通过与正式协议契约（2026-08-20）

用户在隔离 `19092` 服务上以 player-3 重登后，手动打开“人物信息 → 属性”确认装备加成
立即显示，未进入战斗。对应原始抓包目录为
`bin/multiplayer-data/player-3/logs/login-equipment-packet-capture/run-00013865-00030520-001`：
tick 125 的 `builtin-group-type1` 下行包为 2011 bytes，按顺序含
`1/7/7:656`（八行 type-2 装备）及紧随其后的 `1/7/7:22`（零行 type-3），之后独立的
`7/20` 和 `7/32` 对象仍保留。服务端日志记录了
`mock_login_equipment_type3_probe role=10036 rows=8`。

只读运行时追踪同时记录：tick 125 先发生 8 次原生装备安装，随后
`scene_rebuild_status_meter_node` 以八件完整装备列表进入；属性页在 tick 147 显示相同列表。
这与 `mmGameMstarWqvga.cbm:sub_D04` 的 `type=2` 安装、`type=3` 共享回调顺序相符，且没有
写入 guest 内存、寄存器、CBE/CBM 指令或伪造穿脱结果。

因此正式服务端契约为：每次首次 `builtin-group-type1` bootstrap 若已发送非零装备行，必须
紧随 type-2 装备对象发送零行 `1/7/7 {type=3, iteminfo=00}`，并将它计入组合对象数。该对象
不发送给无装备角色、不在同一会话的重复 group 回复中重放，也不改变独立 `7/7(type=3)` 查询
的 `7/32` 答复。它是默认服务端行为，不依赖任何环境开关、抓包程序或专用启动脚本。
