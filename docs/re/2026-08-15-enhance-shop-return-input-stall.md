# 强化页玄晶不足进入商城后背包输入停滞（2026-08-15）

## 触发步骤

1. 从背包中打开一件装备的强化页；
2. 在玄晶不足时选择前往商城；
3. 商城正常打开，按返回回到原背包；
4. 背包画面仍在，但所有点击均不再响应。

## 已确认的客户端链路

- `JianghuOL.CBE:HandleItemUseAndEquip(0x01028C7C)` 处理 `1/29/1` 与
  `1/29/2` 时都会清除 `itemCtrl+1468` 的网络等待位。因此已接收成功
  `29/1` 或 `29/2` 后，强化预览本身不应继续占用全局网络等待。
- `mmShopMstarWqvga.cbm:sub_1038` 的商城初始化只发送 `14/14`、`14/4`
  以及第一页 `14/5`、`14/6`；`sub_162C -> sub_11F0` 另发
  `1/1/14(actorId)`。
- `mmShopMstarWqvga.cbm:sub_9DE` 在收到最后一个 `1/1/14 actorinfo`
  对象后清除商城请求的等待位；该对象必须位于响应末尾。

## 本次运行时证据

`bin/server_out.txt` 中本轮强化后商城初始化为：

```text
29/1 (等级 3 预览) -> scene-interaction combo(14/14,14/4,14/5,14/6)
-> 1/1/14(actorId) response (actorinfo last) -> 7/42
```

没有场景重入响应；商城响应中的 `actorinfo` 也带有 parser 会读取的
`revivetype/ruffianflag/type` 字段。因此先前“商城返回导致场景栈替换”的根因不适用。

## 第一轮已排除的假设

服务端把 `g_netMockShop17ListPending` 当作“下一条任意背包列表请求都属于商城”的
全局开关。普通商城初始化会设置该标记，而商城关闭只是客户端本地 screen-pop，不会有
服务端可见的关闭包来撤销它。

这与已确认的请求契约冲突：

- NPC 商店兼容路径的列表请求是 **带非空 `17/1` 载荷**的
  `17/1 + 7/42` 组合；`vm_net_mock_build_shop_items_books_combo_response()`
  已严格验证这个条件。
- 普通背包打开/恢复使用空 `17/1`、空 `7/42` 或其空载荷组合，由
  `mmGame:sub_418C` 的背包列表 parser 消费；它不能被改写成商城项目列表。
- `mmShop:sub_1038` 的正常初始化只使用 `14/14`、`14/4`、`14/5`、`14/6`，
  另有 `1/1/14(actorId)` 状态查询；没有“将后续任意空背包请求改为商城列表”的客户端
  调用或 parser 证据。

旧分发器在严格的 NPC 组合 handler 之后，又把空 `17/1` 与空 `7/42` 单独路由给
`builtin-shop-items17` / `builtin-shop-items-books`。因此在“强化页 → 商城 → 返回背包”
中，遗留商城标记会污染恢复的背包列表，使可见背包与实际 list manager 的数据/选择状态
不再一致，后续点击被该失配的控件状态拒绝。

这个路由缺陷已修复，但用户随后复现的同一操作并没有发送 `17/1`、`7/42` 或购买请求，
故它不是这次输入停滞的根因。

## 第二轮已排除的假设（2026-08-16）

曾假设商城关闭后的 `5/10 + 7/7(type=1)` 会创建新的主物品管理器，因此尝试在
`1/1/14(actorId)` 成功响应后重新投递 `30/21`。用户复现直接否定了该假设：日志中确实出现
第二个 `mock_backpack_grid` 和完整的 `5/10` 响应，但界面被带回背包而不是强化页，且输入仍被
阻塞。

这也与既有客户端取证一致：`mmShopMstarWqvga.cbm:sub_11F0(0x11F0)` 只发送
`1/1/14(actorId)`，`sub_9DE(0x9DE)` 在读取末尾 ActorInfo 后解除商城加载等待；商城初始化
和 actor 查询都没有创建 `mmGame` item manager 的调用。关闭后显示哪个父界面应由客户端已有
screen 栈恢复，服务端不能借该查询重置背包 grid 生命周期。

该错误改动已撤销。当前输入停滞仍为 `unresolved`：本轮没有 `17/1`、`7/42`、购买、场景重入
或未处理包作为第一偏离；下一步必须针对商城关闭前后的 screen/input 回调及 `5/10` 的客户端
调用方继续取证，不能再用额外背包初始化包掩盖问题。

## 修正

1. 分发器不再仅凭遗留 `g_netMockShop17ListPending` 门控商城组合 handler；
2. `vm_net_mock_build_shop_items_books_combo_response()` 继续严格要求带非空 `17/1`
   载荷的 `17/1 + 7/42` 请求，因此 NPC 商店组合仍走 `builtin-shop-items-books-combo`；
3. 空 `17/1`、空 `7/42` 及空组合会被商城 builder 拒绝，随后走已有的角色背包 builders；
4. 不伪造关闭包、场景重入或客户端状态写入。这样即使商城关闭后 pending 标志遗留，返回
   背包的请求也不会被商城物品列表污染。

## 验证目标

1. 强化页玄晶不足进入商城、返回后，背包仍保留并能正常切换分类、选中物品和返回；
2. 同一会话的普通商城返回不再出现 `builtin-shop-items17` 或
   `builtin-shop-items-books`；空请求应由 `builtin-backpack-*` 处理；
3. NPC 商店的带载荷 `17/1 + 7/42` 仍由 `builtin-shop-items-books-combo` 响应。
4. 商城查询不得重置 `g_netMockBackpackGridSeededRoleId` 或发起场景/背包重建；关闭商城后应由
   客户端 screen 栈恢复强化页。

## 本轮实现与验证（2026-08-16）

- 修改 `src/server/mock_server_dispatch.c`：商城组合响应先按请求结构尝试，不再读取
  `g_netMockShop17ListPending` 作为路由条件；商城 builder 的 `itemsPayloadLen != 0`
  检查是唯一的商城/背包区分依据。
- 新增 `scripts/shop-return-routing-regression.c`。它在进程内调用真实 dispatcher，分别以
  stale pending=1 和 pending=0 重放带载荷、空载荷两种组合，并断言两种状态的响应字节与对应
  builder 一致；场景不启动服务、不连接数据库、不修改客户端内存。
- `make -j2`：通过（本次变更触发服务端重新编译并链接）。
- 完整 `shop-return-hangup-v1` 端到端场景暂未运行：当前环境未设置隔离用
  `CBE_AUTOMATION_MYSQL_PASSWORD`，因此没有启动自动化服务或写入数据库。
- 已撤销曾加入的 `1/1/14 -> 30/21` 回归：用户实际复现表明它违反商城 screen 栈的客户端
  生命周期，不能作为该问题的修复或验收依据。

## 构建边界（2026-08-16）

用户报告“返回背包且强化页仍无输入”的那次运行记录仍包含
`mock_shop_open14 ... grid_reseed=1 next=group-type1-30/21`，而该记录的服务端二进制时间是
`19:19`；撤销 grid 重置后的源码时间为 `20:01`，因此该复现没有执行撤销后的代码。

已在 `20:20` 执行 `make -j2`，新的 `bin/jh-online-server.exe` 不再包含这条重置。重启该
用户进程后，复测应继续看到客户端自行发送 `5/10 + 7/7(type=1)`，但响应不得包含 `30/21`、
`7/11` 或 `7/7(type=2)`。这条边界是恢复强化页而非重建背包的必要条件；未使用服务端伪造
屏幕跳转、客户端内存写入或额外场景包。

## 第三轮输入取证（2026-08-16）

撤销 grid 重建后的实际复现已经回到强化页，但之后没有新的客户端请求；此时服务端不再拥有
可修复的输入契约。最新链路中 `1/1/14` 后的 `5/10` 响应仅为 234 字节，确认不含
`30/21`、`7/11`、`7/7(type=2)`，随后正常完成已有的场景任务子集响应。

模拟器的 screen manager 在移除顶层商城 screen 时依据 `isInQuit` 决定是否调用父 screen 的
`resume`。若该标志导致跳过 resume，父强化页仍会被后续 render 调用绘制，但其原生控件仍处于
pause 状态，正符合“画面恢复、所有点击失效”的现象。为识别首次偏离，`src/main.c` 增加了
只读 `logs/screen-resume-trace.log` 记录：移除时的 screen/模块/data package、`isInQuit`、
计划 resume 地址，以及实际 `run-resume` 或 `run-skip`。它不写 CBE 内存、寄存器或屏幕状态。

`make -j2` 和 `git diff --check` 已通过。隔离端到端自动化仍需
`CBE_AUTOMATION_MYSQL_PASSWORD`；当前环境未配置该值，因此没有启动或操控用户的客户端进程。

## 第四轮根因与实现计划（2026-08-16）

### 当前卡点与运行时证据

用户以同一套步骤复现后，商城关闭时 screen manager 的日志记录为
`is_in_quit=0 scheduled=1`，并紧接着执行了 `run-resume`。因此“父强化 screen 没有
resume、只恢复 render”不是首次偏离。最终恢复到的顶层 window 的
`logic=0x01044AA5`、`render=0x01044AA9` 属于 CBM 下载回调窗口；它能保留强化页画面，
却不是强化页自身的输入消费者。

同一复现的网络顺序中，`WT 2/10 len=61` 已由
`vm_net_mock_build_scene_interaction_followup_response()` 返回完整商城目录
`14/14, 14/4, 14/5, 14/6`。紧随其后的单对象
`WT 1/1/14(actorId)` 又被 `vm_net_mock_build_shop_actor_query14_response()` 返回同一组四个
目录对象和末尾 ActorInfo。这是下载回调窗口在商城关闭后仍留在 screen 栈顶的首个可观察偏离；
关闭后点击没有新的网络请求与之吻合。

### IDA 证据与客户端契约

- `mmShopMstarWqvga.cbm:sub_1038(0x1038)` 初始化时调用 `sub_618(0,5)` 和
  `sub_618(1,6)`，与 `14/14,14/4,14/5,14/6` 批量目录请求对应。
- `mmShopMstarWqvga.cbm:sub_11F0(0x11F0)` 只构造 `WT 1/1/14`，唯一字段为
  `actorId`，不请求任何 `14/*` 目录对象。
- `mmShopMstarWqvga.cbm:sub_9DE(0x9DE)` 的 `major=1, kind=1, subtype=14` 分支读取
  ActorInfo 后返回，并以该末尾对象解除商城加载等待。此前插入的 `14/*` 不是该请求的
  响应契约。
- `JianghuOL.CBE:0x01044AA4/0x01044AA8` 证实当前被恢复的是下载回调窗口，而不是
  被 `isInQuit` 跳过的强化 screen。

### 最小修复

新增按账号快照保存的一次性
`g_netMockShopCatalogDeliveredBeforeActorQuery`：完整批量目录已成功构造后才置位；下一条
精确匹配的 `1/1/14(actorId)` 消费该标记并只返回末尾 ActorInfo。没有此前目录的独立
actor 查询继续保留原有五对象 fallback，兼容已确认的无批量目录路径。角色重新选择和标题
登录生命周期会清零该标记，避免跨会话泄漏。

该修改只改变服务端对已构造请求的响应对象集合，不写入 CBE/CBM、客户内存、寄存器或
screen 状态。回归将实际调用批量目录 builder 再发送 Actor 查询，断言结果恰为一个
`1/1/14` 对象且标记被消费；另断言未置位时仍返回原有五对象 fallback。

## 第四轮实现与验证（2026-08-16）

- `src/server/mock_server_interaction_login.c` 在完整目录响应成功后置位该会话标记；
  Actor 查询在标记已置位时只构造末尾 `1/1/14 ActorInfo`，完成后清零。未置位的独立
  查询仍返回 `14/14,14/4,14/5,14/6,1/1/14` 五对象兼容响应。
- `src/server/mock_server_core.c`、`src/server/mock_server_equipment_npc.c` 将标记接入标题
  登录 reset 和账号 capture/restore，避免角色重新选择或账号切换泄漏该一次性状态。
- `src/main.c` 已删除 `screen-resume-trace.log` 临时取证代码；其结论已在本记录沉淀，不保留
  常驻 screen trace。
- `scripts/shop-return-routing-regression.c` 现在复放批量目录后 Actor 查询、独立 Actor
  查询和账号快照恢复。它断言 dispatcher source、WT 对象数量和顺序、一次性消费以及
  fallback，不连接 MySQL、不启动服务且不触碰客户端内存。
- `make -j2`：通过。
- `tmp\\shop-return-routing-regression.exe`：通过。该回归的输入序列为带/不带载荷的
  `17/1+7/42` 组合，以及 `14/14,14/4,14/5,14/6 -> 1/1/14(actorId)`；通过边界为后者
  的响应恰含一个 ActorInfo，并且下一次独立查询恢复原有五对象响应。
- 隔离端到端场景仍未运行：环境没有 `CBE_AUTOMATION_MYSQL_PASSWORD`，因此没有启动测试服务、
  修改数据库或操控用户进程。用户复测时应确认商城返回强化页后可继续点击、选择玄晶和返回；
  服务端日志应出现一次 `mock_shop_actor_query14 ... catalog=prior response=actorinfo-only`。

## 第五轮输入门控取证（2026-08-16）

### 最新复现边界

用户最新复现使用了第四轮构建：`bin/server_out.txt` 已明确记录
`mock_shop_actor_query14 actorId=10036 catalog=prior response=actorinfo-only`，随后
`5/10` 的响应为 234 字节。这证明目录重复响应、背包 grid 重建和旧商城列表路由均未在
此次失败路径中出现，但商城返回后客户端仍不发出任何新的请求。

因此当前卡点从“服务器送错对象”收敛到“客户是否把实际触摸送入当前 screen 回调”。此前
的 `hangup-protocol.log` 是战斗自动化的历史高频取证，虽然最近被写入，但没有商城返回的
输入事件边界，不能作为本轮根因证据。

### IDA 证据与最小观察计划

`JianghuOL.CBE:DispatchTouchIfActive(0x01003D3C)` 读取 `R9+0x9594`：仅当该值为 `5`
时才调用 `R9+0x95E0` 的原生触摸回调；其它值会直接返回成功而不消费 UI 点击。
`ProcessSceneState(0x01003CFC)` 处理相邻的场景状态 `R9+0x4CB6`，但它不是这个触摸门控。

为区分“触摸未进入 CBE”“门控不为 5”“门控为 5 但回调未进入”三种情况，下一次人工复现
临时在该固定 CBE PC 记录最多 48 次：触摸序号、门控值、回调地址、活动 screen、screen 栈深度
和 scheduler tick；门控为 5 时还记录回调入口命中。该观察只读取已存在的 CBE/模拟器状态，
不修改输入队列、客户内存、寄存器、PC/LR、网络包或 screen 状态。取证完成后必须删除并将
结果回写本记录。

## 第六轮输入门控归因（2026-08-16）

用户在第四轮协议修复构建上再次复现，`logs/shop-return-input-gate.log` 的实际触摸记录为：

```text
event=1 tick=1   gate=0 callback=010376ad active=01056204 top=01056204 depth=1
event=2 tick=78  gate=0 callback=010376ad active=01056204 top=01056204 depth=1
event=3 tick=612 gate=1 callback=010376ad active=01056204 top=01056204 depth=1
event=4 tick=665 gate=1 callback=010376ad active=01056204 top=01056204 depth=1
event=5 tick=680 gate=1 callback=010376ad active=01056204 top=01056204 depth=1
```

其中没有任何 `shop_return_touch_callback` 记录。结合 `DispatchTouchIfActive(0x01003D3C)`
的条件分支，首次已确认的本地偏离是：触摸硬件事件已经进入客户端，但商城返回后
`R9+0x9594` 的门控为 `1` 而不是分发所需的 `5`，使回调在 CBE 内被直接抑制。它不是服务端
未收到点击或商城目录对象重复导致的直接现象。

门控值的写入来源仍是 `unresolved`，不能直接把该字段强制改为 `5`。为归因真实写入者，
`hookCodeCallBack` 会在主 CBE 数据基址可用后，启用一次最多 64 条的只读写入观察；
`hookRamCallBack` 仅在写入覆盖 `Global_R9+0x9594` 时记录 scheduler tick、PC、LR、栈内保存
的 LR、`lastAddress`、地址、写入宽度和写入后的门控字节到同一日志。它不改变客户内存、
寄存器、PC/LR、输入事件、网络包或 screen 状态。下一次复现需用该写入 PC 回查 IDA 的函数
及其网络/生命周期调用方，再修复真正拥有该状态转换的协议契约。

## 第七轮根因与修复（2026-08-16）

第六轮新增的写入观察显示，CBE 对 `R9+0x9594` 的两次实际写入分别为：

```text
tick=0  pc=0103789a value=0
tick=77 pc=01037502 value=0
```

随后 `tick=78` 的触摸仍读到 `gate=0`，但在没有任何覆盖该字节的 CBE `UC_MEM_WRITE`
事件的情况下，`tick=128` 的触摸已读到 `gate=1`。因此客户端指令不是写入 `1` 的来源。

根因位于模拟器的网络调度器：`scheduler_queue_net_event()` 曾将
`Global_R9+0x9584` 起的 96 字节网络下载状态复制到任务快照；
`scheduler_dispatch_net_tasks()` 在 `state==2` 的数据事件回调前再用 `uc_mem_write()`
把整块旧快照写回。触摸门控地址 `Global_R9+0x9594` 恰好是这块区域的偏移 `0x10`。该宿主写入
不会触发 CBE 指令写入观察，正好解释了“客户端未写入、门控却从 0 变为 1”。它还会恢复过期的
网络状态、回调指针与计数器，违反网络回调应由客户端 parser 自行完成状态转换的生命周期。

修复删除了该全块快照的字段、入队复制和回调前写回；网络响应仍经原有 `event=7`、回调与 parser
路径正常投递。没有向 CBE/CBM 写入门控、寄存器、PC/LR 或伪造屏幕切换。第六轮临时触摸与写入
观察也已删除，避免常驻追踪影响正常会话。

## 第八轮修正与取证计划（2026-08-16）

用户使用第七轮构建复测后，返回强化页仍没有输入响应。因此“过期快照回写”是已证实的宿主状态
污染，但不是单独足以解释此问题的根因；不能以删除它后仍卡住为由重新加入该无契约写入。

IDA 对 `DispatchTouchIfActive(0x01003D3C)` 的汇编给出准确字段含义：它读取
`(R9+0x9588)+0x0C`，即 `R9+0x9594` 的网络/资源加载状态，仅等于 `5` 时才调用
`R9+0x95E0` 的触摸回调。该字段不是独立输入开关。此前的 `0`、`1` 都会抑制触摸，但仍需
确认哪一个正常事件或 parser 分支应把它迁移为 `5`。

下一次复现临时记录有界的三类只读证据：每个 `event=5/7` 在远端观察前、远端观察后和客户
callback 后的状态/回调/活动 screen；每次触摸分发及实际回调入口；以及实际 CBE 对该状态字节
的写入 PC。日志写入 `logs/shop-return-input-state.log`，不写入客户内存、寄存器、PC/LR、
输入队列、网络包或 screen 状态。结果将用于区分连接 ready 事件缺失、parser 未完成状态迁移
和回调/Screen 生命周期失配。

## 第九轮输入边界取证（2026-08-16）

第八轮日志确认了网络回调和触摸门控状态，但尚未覆盖宿主硬件事件进入主循环后的边界。
因此在 `src/main.c` 增加了最多 192 条只读记录，统一写入
`logs/shop-return-input-state.log`：

- `host-enqueue`：`keyEvent`/`mouseEvent` 将按键或触摸写入既有 `VmEvent` 队列后的事件参数；
- `main-dequeue`：screen 主循环调用 `DequeueVMEvent()` 后的队列计数、当前 screen 和 logic 入口；
- `logic-before` / `logic-after`：调用该 screen 的原生 logic 前后，记录原始事件、转换后的事件类型、
  screen 指针和栈顶。

这些记录不调用业务回调、不修改 CBE/CBM 内存、寄存器、PC/LR、输入队列或网络包；仅用于区分
“宿主未入队”“入队未出队”“出队但未进入恢复 screen logic”以及“logic 已执行但客户端分支拒绝”
四种边界。计数器在模拟器重启时清零，单次运行有界。`make -j2` 和 `git diff --check` 已通过；
等待使用该构建的人工复现结果后再决定业务层修复点。

## 第十轮根因与修复（2026-08-16）

第九轮日志证明问题不在宿主输入队列：商城返回后，键盘和触摸事件都进入 `VmEvent`、被主循环
出队，并实际调用恢复强化页的 `mmGame` logic `0x0502DA6B`。随后按键转发至
`JianghuOL.CBE:SceneNodeCreateAndInit(0x0101DEDE)`，该函数在场景仍处于商城动作状态时按其
自身条件返回。因此不能通过重建 screen、直接调用 UI 回调或写场景标志解决。

`JianghuOL.CBE:InitNetEventConn(0x010348FC)` 给出了更早的宿主 ABI 偏离：它调用网络管理器
`idx=0` 后，返回值为零时将自己的连接对象置为状态 `3`，非零则置为 `0`。
`get_net_manager_object(0x01036584)` 证明触摸分发使用的状态是
`R9+0x9588+0x0C`；`DispatchTouchIfActive(0x01003D3C)` 只在该状态为 `5` 时调用原生触摸
回调。此前 `src/main.c` 的 `idx=0` 实现一面已经注册并调度成功的网络通道，一面返回 `1`，
并直接把该客户端字段写为 `1`。运行日志也记录到 CBE 写入 `0` 后、没有 CBE 写入 `1` 的情况下，
该字段变为 `1`。这使商城关闭续体无法沿客户端的成功连接状态机完成恢复，留下强化场景的
动作标志组合，从而同时抑制触摸和后续场景按键动作。

修复位于网络管理器 `idx=0` 的 ABI 边界：成功注册本地通道后仅排队已有的连接事件并返回 `0`；
删除对 `R9+0x9594` 的宿主写入。客户随后按 `InitNetEventConn` 的既有分支维护其状态，网络响应
仍通过原有 scheduler 回调和 parser 路径投递，未改写 CBE/CBM、寄存器、PC/LR、场景或协议包。
本轮的输入队列、触摸和内存写入观察均已从源码移除。

验证：`make -j2` 与 `git diff --check` 已通过。隔离端到端场景未运行，因为
`CBE_AUTOMATION_MYSQL_PASSWORD` 未设置；没有启动、停止或操作任何用户进程、端口或账号。
人工复测应验证原路径返回强化页后能继续选择玄晶、切换操作并正常返回。
`tmp\\shop-return-routing-regression.exe` 也已通过，确认此前的商城目录/背包路由修复仍保持；
该回归不启动客户端，不能替代上述输入恢复的人工验收。

## 第十轮结论修正（2026-08-16）

上一轮将 `idx=0` 的返回值从 `1` 改为 `0`，并把它解释为
`InitNetEventConn(0x010348FC)` 的成功值。该解释已被启动回归否定，不能再作为修复依据。

本次独立运行 `artifacts/automation/startup-connect-v1-20260816T233000/`，未使用
`tmp/` 的脚本、日志或转储。日志记录 `idx=0` 后产生标准连接事件 `5`；在没有服务端监听的
隔离条件下随后出现 `idx=2` 关闭和重试，证明这条路径确实是启动的连接注册边界。结合用户在
有服务端时的稳定回归，`idx=0` 必须保持原有的非零“异步注册已受理”返回约定。

修正仅恢复 `vm_set_call_result(1)`；不恢复此前对 `R9+0x9594` 的宿主写入。该字节仍只能由
CBE 的正常回调和 parser 生命周期演进，不能用网络钩子强制设置。

## 第十一轮连接对象边界（2026-08-16）

为排除“返回值分支写入的连接对象就是触摸门控”的混淆，本轮在独立运行
`artifacts/automation/net-connect-abi-v1-20260816T234500/` 中临时增加了只读 PC 观察，运行后已从
源码删除。该运行没有连接成功服务端，也没有使用 `tmp/` 的任何产物。

同一次 `idx=0` 调用中，平台钩子看到的连接对象为 `0x010560F0`，而全局门控为
`Global_R9+0x9594`；`InitNetEventConn` 返回后在 `0x01034920` 将前者保持为 `0`，后者仍为 `0`。
二者不是同一对象。因此不能从局部对象的 `0/3` 分支推导或直接设置全局触摸门控。

此前商城返回时的 `gate=1` 与该边界唯一相符的宿主来源是旧 `idx=0` 代码的
`uc_mem_write(Global_R9+0x9594, 1)`。当前实现保留非零注册返回值，但已删除该写入；当商城操作在
已有交互会话中重新登记通道时，CBE 已有的 `gate=5` 不会再被宿主降为 `1`。本轮构建后的原失败
路径仍待人工验收：返回强化页后触摸须进入 `DispatchTouchIfActive` 的既有回调，且不应出现新的
宿主门控写入。

## 第十二轮：网络终结事件与输入门控（2026-08-17，investigating）

### 当前卡点

用户使用已删除 `idx=0` 宿主门控写入的构建复现后，商城返回强化页仍然可见、但键盘与点击
没有响应。此前日志不足以证明这次运行在 ActorInfo 后究竟投递了哪些网络事件，不能把旧的
`gate=1` 记录当作当前复现的结论。

### IDA 证据

- `江湖OL.CBE:DispatchTouchIfActive(0x01003D3C)` 读取
  `get_net_manager_object()+0x0C`，即 `R9+0x9594`；仅值为 `5` 时才调用
  `+0x58` 的触摸回调。
- `江湖OL.CBE:handle_version_update_response(0x01037472)` 对事件 `7` 进入 WT
  解析、对事件 `5` 直接结束；只有事件既非 `7` 又非 `5` 时才在
  `0x010375A2` 写入该字段值 `5`。资源对象 `18/5..7` 的分支也可按资源加载
  契约写入 `0`，因此不能靠直接改字段恢复输入。
- `江湖OL.CBE:net_wrapper_event_dispatch(0x0103489A)` 只在该字段为 `1..4` 时
  调用 manager `+0x44` 回调，随后仍会把事件交给业务回调。由此事件类型、顺序和
  回调上下文都是协议契约的一部分。

### 宿主边界与待验证假设

`src/network-client.c` 只会在服务响应带
`VM_CLIENT_RESPONSE_FLAG_CLOSE_AFTER_DATA` 时排队事件 `9`；当前服务端代码没有设置该
标志。普通商城流因此目前可观察到事件 `5` 与 `7`，但尚未证明客户端是否还期待一个真实的
传输终结事件来完成 `R9+0x9594 -> 5`。标题模块更新曾因不加区分地投递事件 `9` 回归，故不能
把该现象当作通用修复，也不能先改变事件类型。

下一步仅加入有界、只读取证：在收到准确的商城目录对象序列
`14/14,14/4,14/5,14/6` 后启动一次窗口，记录随后 ActorInfo、每个 `5/7/8/9` 回调前后、
`R9+0x9594` 的 CBE 写入，以及用户后续触摸到达 `DispatchTouchIfActive` 时的门控和回调地址。
日志写入独立的 `logs/shop-return-input-v2.log`，不写客户内存、寄存器、PC/LR、事件队列或
响应字节；满额后自动停止。只有该运行证明第一处缺失事件或错误事件后，才决定协议/传输修改点。

### 第一次 v2 取证的负面证据

用户随后用该构建完成了一次完整复现，`bin/server_out.txt` 记录了预期顺序：
`29/1 -> 2/10 -> (2/10,14/14,14/4,14/5,14/6) -> 1/1/14 ActorInfo-only -> 5/10`。
因此这次复现再次排除了目录重复、ActorInfo fallback 和背包重建，但没有产生 v2 日志。

原因是观察器错误地把外层对象数固定为 `4`；实际商城目录 response 的合法可选前缀
`2/10 ActorOther` 使对象数为 `5`。这是取证器的匹配缺陷，不是客户端或服务端业务偏离。
观察器现已改为在一个有效 WT response 内搜索连续的
`14/14,14/4,14/5,14/6`，允许此前已经有 IDA/运行时证实的 `2/10` 前缀；它仍只读，且不会
改变 response、事件队列或客户端状态。需用该窄化后的构建再复现一次采集实际回调顺序。

### 第二次 v2 取证

修正观察器后，`bin/multiplayer-data/player-3/logs/shop-return-input-v2.log` 记录到：

```text
tick=257  catalog event=7 before/after callback: gate=0
tick=271  outbound WT 1/1/14(actorId): gate=0
tick=272  ActorInfo event=7 before/after callback: gate=0
tick=274  shop return touch reaches DispatchTouchIfActive: gate=0
tick=275..278  returned mmGame screen receives four event=7 callbacks: gate=0
```

窗口内没有 `5`、`8`、`9` callback，也没有覆盖 `R9+0x9594` 的 CBE 写入。`manager_cb`
稳定为 `0x01037473`（`handle_version_update_response+1`），但
`net_wrapper_event_dispatch(0x0103489A)` 仅在 gate 为 `1..4` 时才调用它；gate 为 `0` 时
普通 `7` 数据只到业务 callback，不能自行走到该函数的非 `5/7` 事件分支并写入 `5`。

### 第十三轮：mmGame 输入对象归属（2026-08-17，取证中）

从本次复现保存的 `shop-return-mmgame-module.bin` 中，以同一运行日志的已记录机器码
`0502D87C` 在转储偏移 `0xCD6C` 的匹配建立运行时装载基址 `0x05020B10`。按该基址反汇编
返回后的 `mmGameMstarWqvga.cbm` logic `0x0502E57B`，得到如下真实分支：

```text
0502E57A  push {r3,r4,r5,r6,r7,lr}
...       r0 = [module_global + 0x20]
0502E58A  r2 = signed [r0 + 8]
...
0502E5B0  r1 = byte [r0 + 0x0c]
0502E5B2  if (r1 != 5) return
0502E5C2  call [r0 + 0x58](eventType, eventArg)
```

这证明“输入进入 logic 但无效果”的直接原因是该模块所见管理器 `+0x0c` 不为 `5`；并且它是
实际 UI 分发前的首个拒绝分支。此前 v2 只读取 `Global_R9+0x9594`，尚未证明其与
`mmGame` 的 `[module_global+0x20]+0x0c` 是同一对象，也未验证商城返回后模块 `R9` 是否恢复到
正确的动态 CBM 归属。

为区分这两个可检验假设，临时观察器在 ActorInfo 后首次恢复到不同于商城的 pool screen logic
时动态记录该 callback；在该 callback 的入口和 `+8`（`r6 = module_global` 已完成）各最多记录
16 次。记录项为模块名、动态 callback、由当前 `R9` 计算的 local PC、实际 `R9`、screen 保存的
module R9、module global、其 manager 指针、`+8` pending 计数与 `+0x0c` gate。它只读寄存器/客户
内存并写独立日志，不改变输入、网络事件、CBE/CBM 字节、PC/LR 或任何业务状态。

下一次复现的判定标准：

- 若 `manager == Global_R9+0x9588` 且 gate 仍为 `0`，继续追溯该全局对象从正常交互态到 `0` 的
  首个客户写入/协议回调；不得凭空补事件。
- 若 manager 或模块 `R9` 与恢复前的 mmGame 所有权不一致，则问题在 screen/dynamic-module
  生命周期归属，后续只修复拥有该归属的宿主层，不写客户字段。

### 第十四轮：动态 CBM 所有权根因与修复（2026-08-17）

本轮用户复现的新增日志给出了明确的首次偏离：商城返回后 `screen_module_r9=00000000`，并且
`mmgame-input-entry` 的实际 `R9=01050BD0`（主 CBE data base），而不是内存池中的动态模块静态
基址。`mmGame` 因此把其 `module_global` 解析为 `01053408`，读取了错误 manager `0105A158` 的
`+0x0c=0`，在已验证的 `0x0502E5B2` 分支返回。此前 v2 记录的全局 gate=0 是这个错误静态基址
的结果，不能据此给服务端补事件或写 gate。

本次服务端原始记录同时确认请求/响应没有首次偏离：

```text
29/1 -> 2/10(14/14,14/4,14/5,14/6) -> 1/1/14(ActorInfo only) -> 5/10
```

`mmGameMstarWqvga.cbm` 的保存运行时转储用同次机器码匹配得到 code base `0x05020B10`；返回
logic `0x0502E57B` 的 module-local PC 为 `0xDA6A`。其入口指纹为
`F8 B5 B2 4E 0C 1C 4E 44 30 6A AF 49`，并遵循项目既有的动态 CBM ABI
`moduleR9 = codeBase + 0x14000`。这与已存在的 `vm_infer_battle_module_from_screen()` 约定一致。

修复在 `scheduler_prepare_screen_call()`：当 screen 栈没有所有权时，仅在 logic callback 等于上述
module-local PC 且指纹完全匹配时，动态计算 code base 和模块 R9，并回填该 screen 栈条目。之后
既有 `vm_restore_r9_for_entry()` 使用正确模块寄存器执行 init/render/logic/pause/resume。该路径不写
CBE/CBM 内存、角色/场景/gate、网络事件或响应包；若模块版本或签名不匹配，推断拒绝并保留原有
行为。

下一次复测应在 `mmgame-input-gate` 看到内存池 R9 与非零 `screen_module_r9`，并由客户端自己的
module data 继续分发输入。确认通过后删除第十二至十三轮的临时 v2 取证代码，仅保留本记录。

### 第十五轮：屏幕所有权优先级修正（2026-08-17）

第十四轮首次实施后，检查到用户可用的最新 `shop-return-input-v2.log` 止于 `00:59`，而包含第十四轮
推断的 `bin/main.exe` 于 `01:05` 才构建；该日志仍是旧二进制的 `R9=01050BD0`、
`screen_module_r9=00000000`，不能用于判断新推断是否生效。保存的同次动态模块转储在偏移
`0xDA6A` 的字节为 `F8 B5 B2 4E 0C 1C 4E 44 30 6A AF 49`，与推断指纹完全一致，且
`0x0502E57A - 0xDA6A = 0x05020B10`、`moduleR9 = 0x05034B10` 均在内存池范围内。

代码复查还发现优先级错误：`scheduler_prepare_screen_call()` 原本会先采用
`vm_dl_current_sp_bf()` 的环境下载模块，随后才尝试屏幕 logic 的精确 mmGame 推断。商城关闭后这个
环境值可以残留为商城模块，从而掩盖已验证的返回屏幕所有权。现将精确推断前移到该通用回退之前；
只有 screen 栈没有归属、logic local PC 和机器码指纹同时匹配时才回填，任何不匹配的屏幕仍按原有
loader 回退执行。该调整不写入 CBE/CBM、角色、场景、gate、寄存器、网络任务或响应包。

`make -j2` 于 `01:12` 通过，新的 `bin/main.exe` 含 `screen_mgr module_infer module=mmGame`
标记。隔离端到端自动化尚未运行：本机未配置 `CBE_AUTOMATION_MYSQL_PASSWORD`，运行器按隔离规则
拒绝创建测试 schema。下一次必须用该新二进制复测原路径；判定日志需要同时满足
`screen_module_r9=05034B10` 与入口 `r9=05034B10`（每次装载基址可不同，但二者必须相等且在内存池），
之后再以真实按键或触摸产生后续客户端请求/界面行为作为成功证据。

### 第十六轮：错误 static-base 推断撤回（2026-08-17）

第十五轮构建在进入游戏时发生新的不可访问地址崩溃：`pc=0502F6C6`、`r9=05034B10`，故障写入
目标为 `r1+8=56FF47B7`。同一现场中 `r9+0x2050=05036B60` 被代码当作全局对象读取，但该地址
实际包含 Thumb 指令流并导出了伪指针 `56FF47AF`。这直接反证了把 `codeBase+0x14000` 当成
mmGame 通用 static base 的假设；入口机器码匹配只能证明代码重定位基址，不能证明 R9 ABI。

因此已删除 `vm_infer_mmgame_module_from_screen()` 和其对 screen 栈/全局 R9 的写入，避免该推断
影响普通进入路径。`make -j2` 已通过。该撤回恢复原有进入逻辑，但不应被误报为输入停滞的修复。

代码审查同时发现更早且可检验的缺口：`hook_vm_dl_load_func()` 的
`vlDlLoadApp/vlDlLoadAppEx` 虽返回成功，却从未填充 `g_vmDlLoadedApps` 的 app、code buffer、
context 或 static base；screen push/pop 因而不能通过 `vm_dl_find_loaded_index_by_pc()` 恢复
mmGame 所有权。下一轮仅增加最多 96 条只读 `logs/dl-context-trace.log` 记录，捕获 load、
parse、context、unload API 的 `r0-r3/r9/lr` 及当前已登记条目。该记录不修改客户内存、
寄存器、CBE/CBM、screen、网络事件或响应。必须先从真实 load ABI 确认参数语义和 static base，
再实现 loader 元数据登记；不得再由 screen logic 地址推测 R9。

这证明返回强化页前的首个异常状态是 **gate 保持 0**，但尚未证明它应由哪一个客户端可见事件
转为非零。为避免把“日志中没看到”误判为“没有排队”，观察窗口下一次会额外记录 `5/7/8/9`
的请求入队、去重、槽位耗尽和派发结果，并记录用户按键/触摸在硬件队列与 screen logic 两端的
到达情况。该补充仍无任何业务写入；结果将区分 transport 漏投递与客户端 callback 生命周期。
