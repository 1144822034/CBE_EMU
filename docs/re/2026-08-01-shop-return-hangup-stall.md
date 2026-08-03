# 商城返回后挂机停在“获取数据”取证

## 触发步骤

1. 在场景中打开商城。
2. 从商城返回场景。
3. 点击场景挂机。
4. 客户端停留在“获取数据”。

## 已确认的协议边界

- `JianghuOL.CBE:0x01015E14` (`HandleBattleEnterReq`) 发送外层 `WT 2/10`；
  首对象是 `1/2/10` 且 `Type=2`。它在匹配战斗事务返回前显示加载提示。
- 本次服务端日志确认该请求被 `vm_net_mock_parse_hangup_battle_start_request`
  接受，并生成一个 248 字节、4 对象的启动响应：
  `1/2/10 + 1/2/2 + 1/4/5 + 1/4/11`。
- `JianghuOL.CBE:0x01012E4C` 按对象顺序分发同一 event-7 响应，复合响应本身是
  客户端支持的形式。
- `mmBattleMstarWqvga.cbm:0x66CC` (`HandleBattleStartMsg`) 读取 `4/5` 中的场景
  index/x/y，并从当前活动的场景怪物节点中以坐标匹配目标。服务端 SCE 中的静态
  坐标不是“该节点此刻仍在活动”的证明。
- `25/3` 是请求中的调用标记，尚无对应下行 parser 证据；不得为了关闭加载框而回显。
  `25/11` 仅为信息提示，同样不是战斗进入完成确认。

## 本轮控制台证据及其边界

服务端运行证据的唯一入口是 `bin/server_out.txt`。
`bin/multiplayer/start-server.bat` 会先切换到 `bin` 目录再写入该文件；每次用户说明
“已复现一遍”后，应直接读取该文件的新增尾部，不要求用户手动粘贴日志。

用户实际运行的客户端控制台记录：

```text
queue_data ... resp=248
queue_scene_poll ... resp=289
queue_scene_poll ... resp=23
```

这证明 248 字节响应已排入实际客户端的普通 event-7 网络队列。后面的 289 和 23 是
服务端轮询/自动战斗流程生成的下行响应；它们不能证明前一个 `4/5` 已成功建立战斗
界面，也不能证明“获取数据”覆盖层已被关闭。

`bin/multiplayer/start-player-1.bat` 会在玩家 profile 工作目录中启动
`E:\DevOs\CBE_EMU\bin\main.exe`，且没有重定向 stdout；因此不存在
`client_out.txt` 是正常情况，用户提供的控制台就是实际客户端日志。此前误把另一个
同名进程当作实际客户端的判断已撤销。

## 当前根因状态

第一处偏离仍未定位，候选只剩下需要运行时证据区分的两个客户端契约：

1. 商城返回后，`4/5` 指定的怪物 index/x/y 未对应当前活动的 kind=2 场景节点；或
2. 商城返回期间的 screen 生命周期使 `HandleBattleEnterReq` 的入口状态没有完成到
   `mmBattle` 的预期回调分支。

在获得这两个边界之一的证据前，不修改挂机响应、商城挂起标记、自动战斗时序，也不以
空成功响应/强制场景刷新/回显 `25/3` 绕过加载框。这些做法会掩盖首个错误状态，且不
符合客户端协议。

### 2026-08-01 本次复现的探针校正

本次 `bin/server_out.txt` 已再次确认同一请求被正常处理：`01桃花岛_01.sce`、
怪物 `105`、`index=1`、`pos=(295,57)`，启动响应仍为 248 字节且包含四个对象。当前
player-1 profile 中没有生成 `logs/hangup-protocol.log`。

这并不表示事件没有进入客户端：第一次校正后仍未生成文件，已回到服务端的真实构包
函数逐字节核对。正确下行布局是 `WT + 2 字节总长度 + 1 字节对象数`，对象从偏移 `5`
开始；每个响应对象又是 `major/kind/subtype/reserved/len-hi/len-lo` 的 **6 字节头**。
取证代码此前错误复用了请求对象的 5 字节解析器，故以错误的长度偏移提前退出。两次错误
都只在取证代码中，未参与响应构造、队列投递或客户端状态写入。

探针现按上述下行布局读取对象数和每个 6 字节对象头，校验前四个对象的顺序为
`1/2/10`、`1/2/2`、`1/4/5`、`1/4/11`。下一次人工复现应当产生 callback 前后的只读记录，
从而区分“启动回调未执行”“回调执行但未创建战斗界面”和“后续事件覆盖状态”。

### 2026-08-01 回调边界证据

本次 player-1 日志已产生三条对应同一 `seq=133` 的记录：`queue`、`begin`、`end`。
它确认 254 字节的五对象响应（请求附带 `2/1` 上传时的正常形态）被投递到
`0x0103489B`，并以 `callback_err=0` 返回。进入状态在 callback 前、后均为 `1`，当前
screen 指针也未改变；服务端在此之后自行产生的自动战斗/结算日志不能作为客户端已建立
战斗 UI 的证据。

IDA 表明 `0x0103489B` 不是最终业务 parser：它先按 `R9+38280` 的状态调用 `+68` 回调，
再按 `R9+38056` 指向对象的状态调用 `+20` 回调。后者通常会进入
`0x01012E4C`，该函数解析对象后才通过 `R9+23856` 委托给当前模块。因此下一轮只读取这
三组回调槽和值，用于区分“商城返回后没有安装 battle 委托”和“已安装委托但 `4/5` 未被
其接受”；不会改变任何状态或重新投递数据包。

### 根因与修复点（2026-08-01）

最新 callback 快照确认响应在场景页的通用网络路由器中开始处理：`R9+23856` 为
`0x0502E42D`（运行时 mmGame 的通用业务回调），活动 screen 仍是场景页。这个指针本身
**不能**作为“尚未进入战斗”的判据：`0x01012E4C` 可在该通用路由器内继续分发 `4/5`，而
不会把 `R9+23856` 替换成 mmBattle 的函数地址。因此它只说明首次偏离发生在启动包进入
通用路由器的时序，不能证明 `4/5` 必然被丢弃。

IDA 给出完整的先后约束：`HandleBattleEnterReq(0x01015E14)` 在发送 `2/10 Type=2` 后，
写入进入状态并经 `CleanupPaymentCb(5)` 调用 `SetSceneFlag19638(5)`；后者仅写入
`R9+19638` 的下一帧场景/模块切换请求，并不在当前调用栈中立即加载 mmBattle。模拟网络
队列却为全部 event-7 默认 `delayTicks=0`，可在这一帧将本地极快的服务端响应交给仍活跃
的 mmGame。该事件顺序违反真实异步网络下“先完成客户端已请求的模块切换、再抵达战斗
启动数据”的契约。

修复位于客户端模拟传输调度层，而非服务端 builder：只对已由完整对象前缀识别的挂机
启动响应 `2/10 + 2/2 + 4/5 + 4/11` 使用正常异步队列屏障。它不检查客户端状态、不读写
游戏状态、不变更响应字节，也不影响其他 event-7 包；其唯一作用是让此前已由客户端发起的
state-5 切换在回调前运行。

### 一 scheduler turn 的反证与最终时序常量

后续人工复现的 `hangup-protocol.log` 已记录：精确识别和延迟分支确实命中（`phase=queue`
存在），但一个 turn 后的 `phase=begin` 中，业务 callback 仍为 mmGame，活动 screen 仍为
场景页。因此“一帧”并未满足客户端的模块生命周期，不能作为修复结论。

调度器的既有契约是：普通非 event-7 异步网络回调默认 `delayTicks=6`；每个调度 turn 约为
50 ms。此常量已经是本模拟器用于让客户端完成异步 screen/module 生命周期的正常屏障。现将
上述**唯一精确识别**的挂机启动响应也设为六个 turn。此修改不重投递、不重试、不读取或写入
CBE 状态；它只纠正本地服务响应快于原生异步回调应有时序的运输层偏差。

下一次人工复现必须同时满足：

- “获取数据”覆盖层消失并进入战斗；
- 服务端在启动响应后收到客户端的 `4/6` 战斗操作或自动战斗动作；
- 在该挂起响应仍待投递时，没有较后的 scene-poll 响应越过它。

若六个 turn 后仍没有客户端战斗操作，不能再递增延迟；需改为逐 turn 的只读取证，定位
native screen/module 的完成信号。

### 2026-08-01 六周期反证

随后人工复现仍停在“获取数据”。客户端控制台明确记录精确响应 `seq=184` 已按六周期
延迟后进入并退出 callback，但 callback 前后 `battle_entry_state=1`、`active_screen`
仍为场景页。服务端出现的 `mock_battle_operate` / `mock_battle_auto_action` 由服务端的
挂机会话计时器和 scene-poll 驱动，不能证明当前客户端已经建立战斗 UI；此前把它们作为
成功证据的结论已撤销。

这排除了“本地响应过快”已经被六周期屏障单独解决的假设。后续不再增加延迟，而是只读
跟踪 `0x01012E4C` 中的包门、对象 switch、解包失败和 fallback 分支，以定位首次未满足的
响应字段或 parser 前置条件。

### 2026-08-01 parser 分支结果

`seq=157` 的新增只读记录为：

```text
parser=init:1,guard:1,unpack:0,followup:1,fallback:1,
case2:1,case4:1,entries:4[2,2,4,4]
```

这确认 248 字节响应通过 `event_packet_init`，四个对象均被解出并按既有的 `2/2/4/4`
分支处理；并未走 `ui_show_message_box(解包错误)`。`4/5` 与 `4/11` 在 CBE 的 case-4
通用 handler 中不会直接启动战斗，随后会进入普通业务 fallback，所以该 fallback 不是
错误本身。

已确认的首个状态偏离是：`HandleBattleEnterReq(0x01015E14)` 在发送前写入
`R9+23682 = 3`，而精确响应进入 callback 时该字段已经是 `1`。现在仅从该真实写入之后
对这 2 字节字段设置只读观察，记录每一次覆盖写入的 PC、地址和值；不修改调度时序或
任何 CBE 内存。下一次人工复现将据此确定是谁、在何时将 `3` 提前改为 `1`。

下一次复现没有记录任何覆盖写入：请求路径实际地址 `0x01056852` 已观察到初始值 `3`，
直至响应 callback 结束仍无 guest write 与之重叠。这否定了“同一状态字段被改写”为当前
根因。下一轮取证会同时记录 callback 使用的 `Global_R9` 与原始 watch 地址的值，以验证
商城返回后是否切换到了另一份数据包/R9 上下文；在这个边界确认前，不改变响应字段或
继续调整延迟。

## 下一步取证

已在实际 multiplayer 客户端链路加入临时、只读的 `resp=248` 观察。下次人工复现时，
当前玩家 profile 的 `logs/hangup-protocol.log` 会记录：

```text
mock_hangup_response_queue ...
mock_hangup_response_callback phase=begin ...
mock_hangup_response_callback phase=end ...
```

其中 callback 的状态值只用于定位首个偏离，不改写客户端数据。随后需要记录：

- `JianghuOL.CBE:0x01012E4C` 对 4 个对象的顺序分发结果；
- `mmBattleMstarWqvga.cbm:0x66CC` 接收到的 scene index/x/y，以及其是否匹配到活动
  kind=2 怪物节点；
- `HandleBattleEnterReq` 设置的进入状态在该 event-7 回调后的值。

有了这条实运行证据，修复将落在目标节点来源或响应/界面生命周期真正拥有契约的层，
而不是在服务端添加兜底分支。

## 2026-08-02 最终首偏离与服务端修复

### 触发条件与首个错误状态

固定失败路径仍是“进入商城、返回原场景、点击挂机”。请求为
`2/10(Type=2) + 25/3`，有未提交移动队列时还带一个 `2/1`。只读 parser 记录已经
确认旧的 248/254 字节响应可完整解出，依次进入 `2/2/4/4` 对象分支，没有出现解包
失败。因此增加网络延迟、修改对象长度或重复下发启动包都不是正确修复方向。

第一次偏离发生在服务端的**事务归属**：旧实现把请求确认 `2/10`、场景怪物种子
`2/2`、战斗启动 `4/5` 和自动标志 `4/11` 合并在同一个 event-7 回调中，同时立即将
服务端 battle session 标记为 armed。商城返回后的该回调仍处于场景侧请求事务；客户端
虽能遍历 `4/5`，却没有跨过一次独立网络事务让战斗模块接管 kind 4。服务端随后仍会从
scene poll 生成 `4/6`、结算和下一场日志，造成“服务端看似已经战斗、客户端一直获取
数据”的分叉状态。

六个 scheduler turn 的延迟已经被人工复现反证，现已删除该行为修正。保留的客户端
记录仅用于观察包边界，不修改队列时序和任何 CBE 状态。

### 修复后的协议顺序

服务端改为两个严格分开的事务：

```text
客户端请求:  2/10(Type=2) + 25/3 [+ 2/1 moveinfo]
直接响应:    2/10 [+ 2/1 ack]
下一次轮询:  2/2 + 4/5 [+ 4/11]
```

- 直接响应只完成原请求确认，并在在线 session 中登记一次
  `sceneHangupStartPending`；不创建 battle serial、不武装自动动作、不提前结算。
- 下一次同角色、同可见场景的 scene-sync poll 才构造场景怪物种子和战斗启动对象。
  该构造成功后才设置 armed、记录 battle serial 并启动自动动作计时。
- 该路径复用已验证可工作的“被动队员从独立 scene poll 接收 `4/5`”事务边界，不伪造
  客户端状态，也不读取客户端资源。
- 场景进入 pending、掉线、账号接管等 session 生命周期会清除尚未投递的启动状态，
  防止旧场景的 `4/5` 泄漏到新场景。

### 可观测日志与验证条件

正确运行顺序应为：

```text
scene_hangup_accept ... response=2/10[+2/1] delivery=next-scene-poll
mock_hangup_battle_start source=scene-poll ... response=2/2+4/5[+4/11]
scene_hangup_start_deliver ... delivery=scene-poll
mock_battle_auto_action ...                       # 只能出现在 start_deliver 之后
```

`scene_hangup_accept` 与 `scene_hangup_start_deliver` 之间若出现
`mock_battle_auto_action`，即说明服务端状态又被提前武装，属于回归。构建已通过；商城返回
后的实际客户端进入、连续挂机、取消挂机与切换场景仍按项目规范由用户手工验收。

## 2026-08-02 运行时反证与最终根因修正

上一节的“两事务启动”假设已由随后人工复现否定，不能作为最终修复结论。新版本确实按
计划从独立 scene poll 下发了 213 字节的 `2/2 + 4/5 + 4/11`，客户端也完整执行了
`seq=107` 的 callback；但界面仍停留在“获取数据”。与此同时服务端自行生成 126、99、
315 字节的自动动作和结算。这证明拆包只改变了战斗启动包的到达位置，没有修复此前已
损坏的商城返回场景生命周期。该实验代码现已撤回，挂机请求恢复为普通场景已经验证的
直接复合响应。

### 新证据串联

本次 `bin/server_out.txt` 的顺序是：

```text
scene_npc_reseed_arm ... scene=01桃花岛_01.sce trigger=shop-open
... 商城返回后的 group/backpack/equipment/task 子集响应 ...
scene_hangup_accept ... post_shop=0                 # 被证伪版本的观察值
mock_hangup_battle_start source=scene-poll ... resp=213
```

中间没有任何 `shop_return=1 completion=30/2-no-posinfo` 或
`shop_scene_return_complete`。代码交叉核对确认，商城返回后首先命中的是
`vm_net_mock_build_scene_task_subset_followup_response`。该路径调用 NPC 生命周期 helper；
桃花岛当前 NPC 数为 0，helper 直接清除了 `shopSceneNpcReseedPending`，但任务子集路径并
未下发 `30/2`。后续即使再有资源请求，也已无法识别这是商城返回。

因此第一次偏离实际发生在点击挂机之前：服务端把“是否有 NPC 可重播”和“商城返回场景
是否已完成”错误地绑定为同一个消费条件。`JianghuOL.CBE:0x01039770` 对 `30/2` 的处理
无论是否含 `posinfo` 最终都会在 `0x0103993C` 调用 `ResetDownloadState`；结果值 2 且不含
`posinfo` 的对象正是只结束加载、不重新进入场景或改写坐标的既有契约。

### 最终修改

- NPC 生命周期 helper 不再因为 NPC 数为 0 或 NPC 目录已成功追加就提前消费商城返回
  标记；NPC 数据和场景完成现在是两个明确的责任。
- 商城返回后若客户端首先发送任务子集，该响应在所有请求资源对象之后追加
  `30/2(result=2, scene, no posinfo)`；资源 followup 的既有路径保持同一完成契约。
- 只有整个响应包成功完成后才清除 session 的商城返回标记。构包失败会保留标记，下一次
  合法场景响应仍可完成它。
- 挂机启动恢复为 `2/10 + 2/2 + 4/5 [+4/11] [+2/1]` 的直接响应，不保留已被否定的
  轮询拆包或客户端延迟。

人工复测时，点击挂机之前必须先看到：

```text
shop_scene_return_complete ... source=scene-task-subset-followup completion=30/2-no-posinfo
mock_shop_return_task_subset_complete ... completion=30/2-no-posinfo
```

随后才应出现 `mock_hangup_battle_start source=request`。如果缺少前两条，问题仍在商城
返回请求识别；若存在前两条但仍卡住，再以该完成 callback 之后的客户端状态为新的取证
起点，不能重新增加延迟或拆分启动包。

## 2026-08-02 商城完成后的下一处首偏离

### 新运行时证据

最新复现已经满足上一节的商城返回完成条件：服务端先记录
`shop_scene_return_complete ... completion=30/2-no-posinfo`，随后才收到正常的
`2/10(Type=2)+25/3` 挂机请求，并直接构造 248 字节的
`2/10 + 2/2 + 4/5 + 4/11`。因此“商城返回标记被 NPC 空列表提前清除”确实是一个已修复
的问题，但不是当前剩余停滞的全部原因。

player-1 的只读记录给出了新的严格顺序：

```text
mock_hangup_response_callback phase=begin ... business_cb=0502e42d
mock_hangup_response_callback phase=end   ... business_cb=0502e42d
mock_hangup_battle_state_arm ... state=3 pc=01015ec2
```

同一个 248 字节响应通过主程序 parser，四个对象按 `2,2,4,4` 完整遍历且没有解包错误；
但 response callback 结束之后，`HandleBattleEnterReq` 才完成 `state=3` 的写入。这里的关键
不是该状态值本身，而是 callback 到达时 `R9+23856` 仍由 mmGame 的
`sub_11CE` 持有。

### 客户端契约交叉验证

- `JianghuOL.CBE:0x01012E4C` 先处理主程序拥有的 kind 2 对象，随后把整个 event-7
  回调交给 `R9+23856` 的当前业务委托。
- `mmGameMstarWqvga.cbm:0x11CE` 的对象循环只处理 kind `14/7/16`，不处理 kind 4；
  因而它即使接收到格式完全正确的 `4/5` 和 `4/11` 也不会建立战斗。
- `HandleBattleEnterReq(0x01015E14)` 写入 state 3 后调用 `CleanupPaymentCb(5)`；
  `ProcessSceneState(0x01003CFC)` 再消费 mode 5，并由
  `HandleSceneTransition(0x0100369C)` 切换到战斗模块。只有切换后的 mmBattle 业务委托
  才拥有 `4/5`/`4/11`。

因此当前第一次被违反的契约是网络事件的**模块所有权顺序**：localhost 工作线程返回得
足够快，使含 mmBattle 对象的响应在旧 mmGame 委托仍安装时被投递。固定 1/6 turn 延迟
无法表达这个因果关系，之前的延迟实验被反证是合理的；服务端拆成 scene poll 也仍可能在
委托切换之前到达，同样不是正确修复。

### 修复

修复放在远端客户端传输队列的模块交接边界，服务端响应协议保持不变：

1. 只识别首对象为 `2/10` 且前四对象严格为 `2/10,2/2,4/5,4/11` 的**直接挂机启动**；
   后续自动挂机轮次的 `2/2,4/5,4/11` 不进入此边界。
2. 若响应完成时 `R9+23682` 仍为已确认的场景空闲值 `1`，捕获此刻
   `R9+23856` 的旧业务委托并把事件保留在原 event-7 队列槽中；scene poll 因已有待处理
   event-7 不会越过它。这里不再从 host 记录的 CBM module R9 反推 `sub_11CE` 地址：运行
   时容器前缀与 screen stack 归属会使该反推不稳定，而 state 1 是客户端自身在
   `HandleBattleEnterReq` 中判定“尚未进入/等待战斗”的直接证据。
3. 等客户端自身 mode 5 流程安装新的业务委托后，原响应按原字节、原对象顺序只投递
   一次。实现只读取委托所有权，不修改 CBE 内存、寄存器或包内容，也没有超时伪成功。
4. 如果响应到达时 mmBattle 已接管，则不启用等待，保持普通异步响应路径。

人工复测的预期取证顺序为：

```text
mock_hangup_response_barrier_arm ... source=idle-scene-owner
mock_hangup_response_wait ... source=await-mmBattle-owner
mock_hangup_response_ready ... source=mmBattle-owner-installed
mock_hangup_response_callback phase=begin ... business_cb=<new delegate>
```

随后客户端应进入战斗并产生真实的战斗操作；在 `ready` 前不得出现服务端自动动作。若只
出现 `wait` 而始终没有 `ready`，说明 mode 5 本身没有被 `ProcessSceneState` 消费，下一步
应取证场景 logic 生命周期，而不能添加超时放行。

第一次实现按 `g_currentScreenModuleBase - 0x14000 + 0x11CE` 识别 mmGame。人工复现
`seq=225` 显示 `delegate_barrier=0`，响应仍在 `battle_entry_state=1`、
`business_cb=0502e42d` 时立即进入 callback；因此该地址推导只是假阴性，等待分支根本没有
执行。现已用上述客户端原生 state 1 条件替换该推导。此调整不扩大到其他响应，也不把
state 3/2 的正常迟到响应强制延后。

## 2026-08-02 委托屏障反证与状态覆盖取证

人工复现 `seq=257` 得到：

```text
mock_hangup_response_barrier_arm ... battle_entry_state=1 delegate=0502e42d
mock_hangup_response_queue ... delegate_barrier=1
mock_hangup_response_wait ... source=await-mmBattle-owner
```

之后没有 `mock_hangup_response_ready`。这反证了“只要等 mmBattle 委托自然安装即可”的
修复假设：响应尚未投递时，客户端自身并没有继续完成模块交接。无限等待会直接制造新的
永久停滞，因此该屏障已撤回，远端响应恢复普通 event-7 投递。没有添加超时放行、状态
写入或重复投递。

同一份 `hangup-protocol.log` 在 response queue 之前记录到：

```text
mock_hangup_battle_state_arm generation=2 ... state=3 pc=01015ec2
mock_hangup_response_callback phase=queue seq=257 ... business_cb=0502e42d
```

而 queue 时客户端控制台读到 `battle_entry_state=1`。所以当前最窄、可检验的根因陈述是：
`HandleBattleEnterReq` 已按原生路径写入 state 3，但在直接响应排队前，某个客户端生命周期
路径把状态恢复为 1，并且 mode 5/模块交接没有完成；首个覆盖写入者仍待确认。

此前的写监视有一个取证缺陷：`vm_hangup_protocol_parser_trace_end()` 在任何网络任务回调
结束后都会清除 request-side watch。若挂机请求与响应之间插入普通 scene poll，真正的
state 3 -> 1 写入就不会被记录。现已修正为只有匹配的直接挂机响应才能结束取证窗口，并
同时只读监视：

- `R9+23682`：战斗入口状态；
- `R9+19638`：`SetSceneFlag19638(5)` 设置、`ProcessSceneState` 消费的 scene mode；
- `R9+23856`：当前业务模块委托。

另外在 `HandleBattleEnterReq -> CleanupPaymentCbWrap(5) -> ProcessSceneState ->
HandleSceneTransition` 的七个已确认 PC 上记录三项状态快照。下一次人工复现应以第一条
`mock_hangup_battle_state_write`、`mock_hangup_scene_mode_write` 或
`mock_hangup_transition_step` 为判断依据；在确认首个覆盖 PC 之前不再改变响应时序。

### `seq=121`：mode 5 被 mode 4 覆盖

修正监视生命周期后的复现已经给出完整状态链：

```text
HandleBattleEnterReq                 battle=3 mode=0  business=mmGame
SetSceneFlag19638(5)                 battle=3 mode=5  business=mmGame
ProcessSceneState / mode 5           battle=3 mode=5  business=mmGame
module teardown @ 0x01019664         battle=3 mode=5  business=0
SetSceneFlag19638(4)                 battle=3 mode=4  business=0
ProcessSceneState / mode 4           battle=3 mode=4  business=0
scene_runtime_init @ 0x010137FE      battle=0
EndBattleTurn @ 0x01017E0C           battle=0
SelectBattleTarget @ 0x01016CCE      battle=1
module install @ 0x0101808E          business=mmGame
response seq=121 callback            battle=1 business=mmGame
```

这排除了“mode 5 没有被 scene logic 消费”和“仅仅是服务端返回太快”。第一次偏离是：
mode 5 已经卸载 mmGame，但在 mmBattle 建立前客户端又调用场景进入链，设置 mode 4，完整
重建 mmGame 场景；响应随后才被旧模块解析。`0x010137FE/0x01017E0C/0x01016CCE` 的状态
写入都是第二次场景初始化的结果，不是各自独立的根因。

IDA 交叉验证：`EnterSceneByMapName(0x0101809C)` 在目标场景类型与当前类型不同时，经
`CleanupPaymentCbWrap(3/4)` 发起 mode 3/4；mode 4 的消费者正是上面观察到的
`scene_runtime_init_and_sync`。当前未知项缩小为：商城返回后的哪一个残留 screen/module
回调再次调用 `EnterSceneByMapName`。已在该函数入口及 mode 3/4 提交点增加 LR、参数、
场景名和六个栈词的只读记录；确认调用来源前不移除服务端对象、不抑制场景进入。

### `seq=208`：排除 `EnterSceneByMapName`，继续定位 mode 4 的真实调用者

最新人工复现再次得到完整的 `mode 5 -> mode 4 -> mmGame` 顺序，但新增探针没有命中
`0x0101809C` 或 `0x01018142`。因此上一节把 mode 4 归因于
`EnterSceneByMapName` 只是待验证假设，现已被运行时证据排除。

`CleanupPaymentCbWrap` 的主 CBE 交叉引用还包括两个能够产生 mode 3/4 的原生路径：

- `net_handle_actor_move_info case 9 @ 0x01012DC6`：消费场景切换结果后，根据场景类型
  请求 mode 3/4；
- 场景触发点倒计时路径 `0x01018296`：触发点完成后请求 mode 3/4。

此外 `CleanupPaymentCb(0x010448DE)` 在写入 scene mode 后，还会调用保存在
`R9+39732` 的一次性 payment/module cleanup callback。商城返回后的 screen 生命周期可能
通过该 callback 或其后续 screen 恢复链触发上述路径。当前只增加以下只读记录：wrapper
入口的 LR/mode、两个 mode 3/4 调用点、cleanup 入口、callback 调用点以及
`R9+39732` 的值。下一次复现必须先确定 mode 4 的真实调用点和 callback 归属，再决定修复
属于服务端残留响应、商城返回完成协议，还是 screen/module 生命周期；不得把 mode 4
直接抑制掉。

### `seq=140`：mode 4 来自新模块的直接 cleanup 调用

最新只读记录给出了此前缺失的调用边界：

```text
CleanupPaymentCbWrap(5)       LR=01015EE5 payment_cb=0502EF73
CleanupPaymentCb(5)           callback 0502EF73 被调用并清零
ProcessSceneState(mode=5)     卸载 mmGame，调用模块索引 4
CleanupPaymentCb(4)           LR=0502CD7D payment_cb=0502E001
ProcessSceneState(mode=4)     重建 mmGame
hangup response seq=140       business_cb=0502E42D，kind 4 被旧模块忽略
```

没有命中 `0x01012DC6`、`0x01018142` 或 `0x01018296`，所以 mode 4 既不是服务端
`2/9` 场景结果，也不是地图入口或场景触发点。它是 mode 5 加载模块期间，从动态模块地址
`0x0502CD7D` **直接**进入 `CleanupPaymentCb(4)`。这也证明继续改变 248 字节响应的网络
延迟无法修复首偏离。

尚需确认 `0x0502CD7D` 在本次 CBM 装载中的模块名和本地偏移。ROM 入口会先把 CBM R9
恢复成主 CBE R9，旧探针因此只能看到动态 LR，不能可靠区分 mmBattle 与商城残留模块。
现于恢复前只读捕获原始 CBM R9，并按加载器既有 `code_base = module_r9 - 0x14000`
关系记录 caller/payment callback 的本地偏移。映射到具体 CBM 函数后，才能判断是
mmBattle 因前置条件失败主动回场景，还是未完成的 mmShop 生命周期覆盖了战斗切换。

### `seq=178`：R9 不能作为本次 CBM 映射依据

最新复现仍是同一条 `mode 5 -> mode 4 -> mmGame` 链，直接调用地址保持为
`LR=0x0502CD7D`，一次性 callback 保持为 `0x0502E001`。但是 ROM 入口恢复主 CBE R9
之前捕获到的 R9 不满足动态模块内存池的 `module_r9` 契约；因此此前计划的
`module_r9-0x14000` 映射在这条调用包装器上不可用，不能据地址区间猜测模块名。

服务端同次记录确认商城返回完成对象已先于挂机请求下发，挂机启动响应仍是合法的
`2/10 + 2/2 + 4/5 + 4/11`，主 parser 完整遍历四个对象；响应字节和网络 callback
均不是当前首偏离。为精确识别 mode 5 装载后主动退出的模块，客户端只读探针现记录
调用地址前后 32 字节和 payment callback 入口 16 字节。下一次复现后应把这两个字节窗
与 `mmBattle/mmShop/mmGame` 的实际 CBM 文件或对应 IDB 交叉匹配；确认模块及函数前，
不修改商城完成包、挂机启动包或事件时序。

### `seq=233`：动态调用唯一定位到 `mmShop`，首偏离早于挂机响应

最新人工复现仍按原样投递 248 字节四对象响应；服务端随后已经从真实挂机 session
构造 `4/6` 行动响应（126 字节），但客户端仍停在“获取数据”。这进一步证明服务端
战斗计时器已经运行，不等于客户端已经建立 mmBattle 界面。

本轮把运行时两个字节窗与四个实际 CBM 文件逐字节比较：

- `c74980b5494449680968002900d0884780bd...` 只对应
  `mmShopMstarWqvga.cbm` 的通用回调包装器；
- `10b5154c00204c445c3c616b48611148...` 只对应
  `mmShopMstarWqvga.cbm:sub_1380`，即该模块导出的销毁回调。

通过只读复制的 mmShop IDB 交叉验证后，运行时调用链已经可以精确还原：

```text
mmShop:sub_4D0
  pendingMode = *(i16 *)(R9 + 10304)
  -> mmShop:sub_EC(pendingMode)
     -> 主程序注册的场景切换回调
        -> CleanupPaymentCb(4)
```

`sub_4D0` 只在 pendingMode 非零时调用主程序回调并随后清零；另外两个
`sub_EC` 调用点固定传 2，不可能产生本次 mode 4。`sub_1380` 会清除 mmShop 的网络
parser、销毁模块 UI/资源并调用宿主 screen 清理。因此 mode 4 不是主 CBE 对 kind 4
响应的误解析，也不是 mmBattle 前置条件失败，而是 **mode 5 装载期间实际运行了
mmShop 的 screen 逻辑**。

第一次被违反的客户端契约现在收敛为模块选择/生命周期：
`HandleBattleEnterReq` 已提交 battle state 3 和 mode 5，但该 transition 选中的实际 CBM
仍是 mmShop；mmShop 随即消费自身的 pending mode 4，将客户端重新带回 mmGame。
这个过程发生在挂机响应 callback 之前，所以调整 248 字节包、拆分响应、延迟投递或
抑制 mode 4 都不能修复根因。

尚缺的最后一组对照证据是：正常“未进入商城直接挂机”和失败“商城返回后挂机”在
`FormatSaveDataPath(0x01036404)` / `LoadPayCBMAsset(0x01044A12)` 处选择的 CBM 路径及
pending mode 是否不同。已增加窄范围只读记录，输出 `module_path`、`pending_mode` 和
活动 screen；它不修改客户端状态。只有确认商城返回后的哪个原生完成步骤没有恢复模块
选择后，才允许修改服务端商城返回响应。当前 `30/2-no-posinfo` 仍保留为已验证的
`ResetDownloadState` 完成对象，不以猜测删除。

### `seq=213`：模块交接完成，服务端自动回合不是客户端进入证据

本次人工复现的客户端仍停留在“获取数据”。服务端同一时段出现的
`mock_battle_operate`、结算和终局关闭均由服务端挂机计时器生成，不能据此声称客户端
已经进入战斗或处理了任何回合；此前将其描述为“处理两回合”的判断已撤回。

新增路径日志补齐了上一节缺少的实际 CBM 对照：

```text
mode 5 -> LoadPayCBMAsset(JHOnlineData\mmShopMstarWqvga.cbm)
mmShop pending_mode=4 -> CleanupPaymentCb(4)
mode 4 -> LoadPayCBMAsset(JHOnlineData\mmBattleMstarWqvga.cbm)
response seq=213 -> 主 CBE parser 完整遍历 2/2/4/4
```

因此 `mode 5` 先运行 mmShop 销毁路径、再由 mode 4 装载 mmBattle 是本次实运行事实；
“最终仍停在 mmShop、没有装载 mmBattle”已被排除。当前首次偏离只能继续位于两处之一：

1. mmBattle 已装载，但它的业务委托没有把 `4/5` 交给
   `HandleBattleStartMsg(0x66CC)`；
2. `0x66CC` 已运行，但商城返回后的 live scene actor 表中不存在响应指定的
   `index=1, pos=(295,57), kind=2, active=1` 节点。

为区分这两个边界，客户端仅增加动态 CBM 本地地址的只读探针：用执行时
`code_base = R9 - 0x14000` 识别 `0x66CC/0x67BA/0x6BEE`，记录入口、目标节点选择结果和
ready 标志。探针不改响应、调度、寄存器或客户端内存。确认是否命中及首个错误状态前，
服务端 builder 和商城完成协议保持不变。

### `seq=116`：四对象包到达主解析器，但尚无 mmBattle 启动处理证据

本次人工复现仍停在“获取数据”，客户端记录为：四对象响应正常入队、网络 callback
正常返回，随后只收到场景轮询响应。服务端的自动行动日志仍只代表服务端 session 计时，
不能视为客户端进入战斗。新增的第一版 mmBattle 探针没有出现
`mock_hangup_mmBattle_start`，所以不能再把后续服务端回合误读成客户端回合。

IDA 对 `mmBattleMstarWqvga.cbm` 的原生分发链确认如下：

```text
whole-packet callback 0x17B6
  -> parsed-entry kind switch 0x1812
  -> kind 4 dispatch call 0x1820
  -> HandleServerBattleCmd 0x7BD0
  -> subtype 5/10 call 0x7DF0
  -> HandleBattleStartMsg 0x66CC
```

只观察 `0x66CC` 尚不能区分“业务 callback 不是 mmBattle”和“mmBattle callback 已进入，
但在对象分发前拒绝或跳过响应”。因此探针已向前扩展到上述每一个边界，并在网络 callback
处额外记录当前 `business_cb` 的 32 字节代码指纹。下一次复现应以最早命中的本地地址确定
首个偏离：若 `0x17B6` 未命中，先核对业务委托归属；若命中至 `0x1812` 但没有
`0x1820`，核对解析对象 kind；若命中 `0x7BD0` 但没有 `0x7DF0`，核对 subtype；只有
进入 `0x66CC` 后才检查 live scene actor 节点。当前仍不修改服务器响应内容或时序。

### `seq=168`：业务委托是 mmBattle；旧探针基址算法无效

本次完整落盘日志确认主解析器成功遍历 `entries:4[2,2,4,4]`，随后走到
`net_handle_business_followup_events()` 返回 false 的业务 fallback；当时安装的
`business_cb=0x0502E42D` 非空。回调入口 32 字节指纹为：

```text
f0b5ffb0ffb0d3b0072b66d15c49554e4944096812234c69011c0a224e44cc3e
```

该指纹在当前运行资源中只匹配
`mmBattleMstarWqvga.cbm` 文件偏移 `0x184D`，对应 IDA 中
`LoadBattleResourceData(0x17AC)`。因此“业务委托仍属于 mmShop/mmGame”已被排除。

上一版 mmBattle 探针没有产生日志不能作为“回调未执行”的证据。它用
`code_base = R9 - 0x14000` 推导动态代码地址，但该 CBM 回调执行期间 R9 仍是主 CBE
全局基址；探针计算出的本地 PC 必然错误。现已改为从已确认的
`business_cb - 0x17AC` 推导重定位基址，并校验 `LoadBattleResourceData` 的八字节入口
指纹后才记录。探针边界覆盖 `0x17AC/0x17B4/0x1810/0x1820/0x7BD0/0x7DF0/0x66CC`
等原生分发点。此修正仍是只读取证，不改变服务端响应或客户端状态。

### `seq=232`：探针窗口被前置普通响应耗尽

修正基址后已经能够命中 mmBattle 的 `0x17AC/0x17B4/0x1810`，并证明商城返回后的
普通 event-7 响应确实会交给 mmBattle 业务 callback。但是 battle-state watch 在发送
挂机请求时即已启用，目标 248 字节响应到达前的多个普通响应已经用完 48 条记录上限；
因此 `seq=232` 本身仍没有留下 kind 4/subtype 5 分发证据。

第一次偏离仍未确定，不能据这些前置普通包修改服务端。探针窗口现收窄为：仅在调度器
识别到 `hasHangupBattleStart` 的目标 callback 开始时清零并启用，callback 返回后立即关闭。
这样下一次记录只包含目标 `2/10 + 2/2 + 4/5 + 4/11` 响应，不再被商城/场景恢复包污染。

## 2026-08-03 根因确认与协议修复

### 首次偏离

此前“商城返回已完成”的结论需要更正。商城返回时服务端在
`vm_net_mock_build_scene_task_subset_followup_response()`（少数路径为 resource repeat
follow-up）追加的对象是外层 `1/30/2`、字段
`result=1,type=2,scene`，但**没有** `posinfo`。IDA 中
`JianghuOL.CBE:scene_handle_change_result_scene_pos(0x01039770)` 对该对象的行为是：
无 `posinfo` 时只执行 `ResetDownloadState(0x0103993C)`；只有存在 `posinfo` 才调用
scene controller 的 `+116(scene,x,y,0)` 入口，建立新的 scene shell。

这正是商城路径的首次错误状态：画面回到了旧 mmGame scene，但商城模块遗留的
payment/module callback 尚未随场景重入生命周期清理。随后用户点击挂机，
`HandleBattleEnterReq(0x01015EC0)` 的 `CleanupPaymentCb(5)` 先消费该 callback；实运行
记录显示它会经过 `mmShop:sub_4D0 -> sub_EC -> CleanupPaymentCb(4)`，再触发新的场景
初始化。挂机 `4/5` 包本身按 `2/2/4/4` 完整解析，却在该错误的模块/scene 生命周期中
无法到达战斗首帧。

这排除下列已尝试方向作为根因修复：调整挂机包长度或对象顺序、拆分 `4/5`、增加网络
delay、在 scene poll 补包、抑制 payment callback。它们都发生在无位置 `30/2` 已经遗漏
scene re-enter 之后。

### 修改

商城返回专属标记仍由真实商城请求建立，且仍只在完整响应构造成功后消费；但其完成对象
现在使用既有的 `vm_net_mock_append_scene_pos_result_object_for_scene()`：

```text
1/30/2 { result=1, type=2, scene=<当前场景>, posinfo={x,y} }
```

`x/y` 来自角色持久化场景位置，不是默认出生点或临时猜测坐标。改动只位于该商城返回的
task-subset 和 resource-repeat 分支；普通场景刷新、传送资源下载后的无位置完成契约不变。
在 packet 完成后才标记 NPC seed pending 并清除商城返回标记，因此 NPC 目录是否为空也
不会再吞掉生命周期完成责任。

### 自动化回归

隔离运行 `shop-return-hangup-v1-20260803T072135595Z-22268` 使用独立数据库、端口、
进程和测试账号。实际输入仅有三次：商城图标 `(224,44)`、商城返回 `E`、待客户端重入
完成后的一次挂机 `(50,350)`。其可复核链为：

```text
server: mock_shop_return_task_subset_complete ... resp=255
        completion=30/2-posinfo-reenter
client: 30/2 -> native scene ScreenInit -> later 25/5 follow-up
client: hangup request -> server mock_hangup_battle_start ... resp=248
client: mmBattle HandleBattleStartMsg(0x66CC) -> BattleScene_DrawMain
```

`result.json` 为 `passed / hangup-battle-main-draw-after-4-5`，并记录 `input_count=3`；
首帧 LCD 证据为 `frames/005_hangup-battle-started.png`。对照场景
`direct-hangup-control-v1-20260803T072300434Z-33896`（不进入商城）也以同一
`0x66CC -> DrawMain` 断言通过。运行器完成后已移除自己创建的测试数据库和进程。
