# player-1/2/3 使用战斗心得后的数量窗口与提示（2026-08-27）

状态：`WT 1/25/7` 现为战斗心得唯一的确认／持久化边界；`WT 1/25/6` 只打开数量窗口，
不会扣除道具或启动时效。player-3 的运行时 PC 证据已证明“悟”标识不能附在 `25/6` 回包中
刷新；本轮已枚举当前场景实际登记的 event-7 处理表，尚未发现可使 `expbook` 实时写入的
客户端契约。

## 触发与首次偏离

战斗心得（`828`）先走专用 `WT 1/25/6 {seq:u16}`，客户端随后从数量窗口提交：

```text
WT 1/25/7, len=31, object=1/25/7, payload=22
raw=57 54 00 1F 01 19 07 00 1B 03 6E 75 6D 00 06 00 04 00 00 00 02 03 73 65 71 00 04 00 02 00 38
```

player-2 捕获的值是 `num:u32=2`、`seq:u16=56`；`num` 是使用数量，不是模式或固定标志，故处理器只要求它为正数。player-1 最新复现中，`25/6` 的 `seq=944` 成功后，客户端连续发送同一笔 `25/7`；服务端每次都已回复 93 字节，因此这不是丢包或未处理请求。

最初 `25/7` 没有处理，客户端等待状态无法完成，表现为进度条不消失。随后两次错误回包分别产生了：

1. `{result,maxnum,iteminfo}`：进度条可消失，但 `25/7` 专用处理器没有取得正文，出现约一秒的空白提示框。
2. 使用相邻模块的中文键名：`result` 缺失，客户端清除忙碌进度后直接返回，数量窗口保持打开并再次提交同一笔 `25/7`。这正是 player-1 最新的“进度条立即消失、数量窗口不关闭”现象。

## 客户端证据

`JianghuOL.CBE:0x0102DCDA` 只处理 `1/25/7`。它先清除本次网络等待，再按以下字段读取对象：

| 读取方式 | 实际键名 | 用途 |
| --- | --- | --- |
| u8 accessor | `result` | `1` 进入客户端完成使用分支 |
| length accessor | `useinfo` | 得到正文的字节长度 |
| data accessor | `useinfo` | 复制提示正文 |

`result==1` 的后续分支在 `0x0102DD4C..0x0102DD92`，会交还给客户端正常结束数量使用流程；`result==2` 是失败提示分支。先前误用的 GBK 字符串位于相邻道具模块，不能作为本响应的字段名。

因此 `25/6` 的 `maxnum:iteminfo` 只属于数量窗口初始化；`25/7` 不读取这两个字段。此前把
背包扣除与时效写入放在 `25/6` 是首个行为偏离：用户仅看到窗口时效果已经开始。现在 `25/6`
只验证可用性并以 `clientId + roleId + seq` 建立五分钟的确认上下文；`25/7` 才重新验证背包、
以原子事务扣除用户确认的 828 数量并写入相同份数的 60 分钟／20%效果。完成后的短期上下文只
接受相同数量的确认包重传，新的 `25/6` 会取代旧上下文。

### 数量窗口中的提前“已生效”文字（修复）

player-3 截图表明窗口已由 `25/6` 正常打开，顶部已有固件自身的「请选择使用数量」，但下方
`iteminfo` 却显示「战斗心得效果已生效，经验增加20%。」。这不是客户端在窗口阶段启动效果；
首个偏离是服务端把确认成功正文错误复用于 `25/6`。

`JianghuOL.CBE:0x01026574` 只读取 `result/maxnum/iteminfo` 来初始化数量窗口。现成功的
`25/6` 固定返回空 `iteminfo`，让窗口只显示客户端自己的数量提示；唯有 `25/7` 成功的
`useinfo` 返回「效果已生效」正文。`battle-insight-followup-regression` 逐字段断言成功窗口
的 `result=1`、`maxnum=<该背包格当前可用数>` 与空 `iteminfo`，并独立保留 25/7 的提交断言。

### 数量上限异常（已修复，待人工复测）

同一截图显示客户端范围为 `1-26996`，与玩家实际堆叠数量不一致。
`26996` 的十六进制为 `0x6974`，不能视为有效业务数量。字段名已由
`JianghuOL.CBE:0x010266F8` 的字面量和 `0x0102659C` 的访问器调用确认是 `maxnum`。

player-3 本次复现的只读 PC 取证在该访问器返回后的 `0x0102659E` 记录到
`maxnum_get=26996`（旧 85 字节回包）以及 `maxnum_get=13134196`（旧 49 字节回包），均为
客户内存地址而非数量。旧回包把 `maxnum` 包成了长度 2 的 WT 数值；客户端专用访问器返回该
值对象地址，后续 `STRH` 取低 16 位，因而画面随机显示 `26996`。

相邻的 `maxnum` 处理器使用长度 4 的 tagged-u32。现在 `25/6` 以同一格式返回所选 828 背包格
的 `item->count`（客户端/协议已有限制时最高 200），并把这个上限锁入确认上下文。`25/7` 必须
不超过该快照且不超过确认时的实际库存；通过后原子扣除该数量，并把 60 分钟时效按数量累计。
取证仍只读取 `R0`，不修改寄存器、包或客户端内存；人工复测应看到 `maxnum_get` 等于显示的
实际可使用数量。

## 修改

- `vm_net_mock_build_timed_special_item_use_response()` 对 828 的 `25/6` 只回复数量窗口所需字段，以 tagged-u32 `maxnum` 传入该格实际可用数，并记录同一数量上限的确认上下文；它不调用背包扣除／时效效果事务，也不附加无效的 `1/1/6`。
- `vm_net_mock_build_battle_insight_followup_response()` 在匹配上下文且数量未超过该上限/当前库存时提交原子事务，成功回复 `1/25/7 {result:u8=1,useinfo:<GBK 成功提示>}`；它扣除所选数量并按数量累计 60 分钟效果。缺少／失效上下文或数量不一致的重传回复客户端已有的 `result=2` 失败分支。它不附加中文伪键、`maxnum`、`iteminfo`、背包刷新或宿主侧 UI 操作。
- 成功正文为「战斗心得效果已生效，经验增加20%。」，其长度由客户端从 `useinfo` 字段自身取得，避免空白提示。

## 验证

- `make -j2`：通过。
- `make battle-insight-followup-regression`：通过。
- `obj/server/battle-insight-followup-regression.exe`：通过。它验证 `25/6` 以 tagged-u32 返回该格数量、仅建立会话确认上下文且逐字节不改角色状态、新窗口会替代旧 `seq`、已确认记录仅接受相同数量的幂等重传；没有确认上下文的 `25/7` 走 `result=2`，`num=0` 保持未处理。过程没有启动监听器或写入角色/背包数据。

人工复测应以新编译的 `bin/jh-online-server.exe` 和 `bin/main.exe` 重启指定测试服务及 player-3 后进行。预期是：窗口范围上限等于该格 828 的实际数量；打开窗口时不扣道具、不开始倒计时；提交数量后进度条结束、数量窗口由客户端关闭、出现带文字的成功提示，并原子扣除所选数量、累计对应小时数。若客户端重传同一 `25/7`，服务端只回幂等成功确认，不重复扣除。

## 场景“悟”标识点击（追加取证）

player-3 重登后，左上角“悟”标识已由 `expbook=1` 正常绘制；点击它会发送无字段的
`WT 1/7/36`（总长 9 字节）。原服务端日志的首个偏离是：

```text
unhandled wt=7/36 len=9 objects=1 first=1/7/36:0 ... resp=0
```

因此客户端没有收到 event-7 数据事件，进度条没有机会由自己的完成处理收尾。此前紧邻的
`25/7` 日志已经是 `response=68 action=client-complete`，不是这次点击卡住的根因。

`江湖OL.CBE:0x01011C88` 对 subtype `36`（十进制，`0x24`）分派到
`0x01011A1E`；该分支读取 `bookinfo` 并交给原生说明界面。服务端现只为精确的空载荷
`1/7/36` 回应一个同 subtype 的 `bookinfo` 字符串：效果仍有效时复用战斗心得的 GBK
成功说明，过期/不存在时明确说明当前未使用。该处理不续期、不消耗物品、不注入角色状态，
也不改变宿主的回调或屏幕生命周期。

`battle-insight-status-regression` 构造该 9 字节原始请求，断言响应只包含一个
`1/7/36.bookinfo` 对象，并拒绝截断请求。

### “悟”说明尾随随机文字（修复）

player-3 最新复测显示提示正文结束后继续出现随机文字。服务端已记录精确请求
`WT 1/7/36` 和成功回包（活动状态下 57 字节）；首个偏离是 `bookinfo` 采用普通
长度分隔 blob，内层有效数据末尾没有 NUL。

`江湖OL.CBE:0x01011A1E` 先取得 `bookinfo` 指针，再直接交给原生文本格式化函数，
没有把 WT 内层长度传给该函数。因此这不是编码或界面问题，而是该字段的字符串边界契约。
`vm_net_mock_build_battle_insight_status_response()` 现仅将这个字段改为
`vm_net_mock_put_object_cstring()`：保留原有 blob、字段长度和 GBK 正文，并使内层长度为
`strlen(bookinfo)+1`、最后一个字节恰好为 `0x00`。不增加对象、不改回调，也不改变时效状态。

`battle-insight-status-regression` 额外逐字节断言 `bookinfo` 的外层／内层长度一致、
正文精确匹配且对象末尾恰有该 NUL，防止随机尾字回归。

## 使用后即时“悟”标识（修正取证）

用户本次复测给出关键对照：大力丸的 `22/3 {ruffianflag}` 会立即显示“力”，战斗心得却不会。
同一轮 `server_out.txt` 的首次偏离是：`25/6` 成功（旧回包 `response=82`）后没有 `25/7`，
随后再次使用同一心得已被服务端按有效效果拒绝。这证明先前放在 `25/7` 之后的刷新对象没有
被投递，不能作为“使用后立即”的契约。

静态复核同时排除了之前的 `1/1/14` 假设。`JianghuOL.CBE:0x010132F8` 接收 kind-1 的
subtype `2/3/6/14`，但只有 subtype `6` 在 `0x010133CC` 读取 `expbook`
（字段字符串 `0x0101353C`）；subtype `14` 只是用旧缓存重建面板，不能令“悟”出现。

随后尝试将状态对象移到实际必经的成功 `25/6` 回包，并使用同一固件的最小 subtype-6 状态分支：

```text
1/25/6 {result=1,maxnum,iteminfo}
1/1/6 {result=0,revivetype,ruffianflag,type,practiseflag,pcimg,expcard,expbook,practiseinfo,lastexp,curexp,persentexp}
```

`result=0` 会使 `0x010133F6` 的 `result==1` 角色选择／场景创建分支不执行。但最新 player-3
复测已确认：服务端确实返回 `response=265 status6=1`，且随后的 `25/7` 正常完成，左上角仍没有
“悟”。这证明 `25/6` 的专用回调不会因同一 WT 帧包含 `1/1/6` 而自动切入通用状态解析；此前把它
称为修复是不准确的。

为固定首个偏离，新客户端会在 `25/6`、`25/7` 以及确认前后观察到的 `2/10` 回包的固件登记
回调期间记录五个只读 PC，并同时标明 25/6 回包是否包含诊断用的第二个状态对象：
`0x01026574`（25/6）、`0x010132F8`（通用状态分发）、`0x01013398`（subtype-6）、
`0x010133CC`（`expbook`）和 `0x01013594`（状态栏重建）。输出为
`logs/battle-insight-status-trace.log`，并包含连接、回调、context、规范化 CBE PC 与回调结果；
不修改 `actorinfo`、登录包、客户内存、寄存器、屏幕操作、宿主回调、包字节或事件顺序。最新
实际 trace 已记录 `handler25_6=1`，而 `generic_1_1/subtype6/expbook/status_rebuild` 均为 `0`；
无效附加对象已移除。后续只能由客户端实际登记的状态刷新契约驱动，不能再把 `1/1/6` 塞进
`25/6` 回包。

## 即时图标的协议边界（2026-08-27）

数量窗口修正后，player-3 的一次真实确认序列为：`1/25/6` 返回实际可选数量，用户确认后
`1/25/7` 成功并持久化战斗心得效果；紧接着客户端只发送 `1/2/10`。后者的既有契约是场景
其他角色列表 `othernum/otherinfo`，空回复为 40 字节；它不包含角色时效字段，也没有读取
`expbook` 的客户端分支。因此它不是漏实现的角色状态查询，不能把 `expbook` 填进该对象。

静态入口复核进一步固定了状态所有权：`expbook` 的唯一客户端字段读取仍在
`JianghuOL.CBE:0x010133CC`，属于 `scene_runtime_init_and_sync(0x01012FB4)` 中的 kind-1
subtype-6 角色状态解析；该状态对象由标题选角的 `1/1/6` 回调交给该路径。现有取证
`docs/re/2026-08-20-first-login-equipment-attribute-bootstrap.md` 也记录该回调归属为
`mmTitleMstarWqvga.cbm:role_manage_screen_handle_network(0x53EC)`，而不是场景内道具使用
回调。新的运行 trace 在 `25/6`、`25/7` 和随后 `2/10` 的整个回调窗口均未命中
`0x010132F8/0x01013398/0x010133CC`。

所以首个缺失点是客户端在 `25/7` 成功后没有发起可被该状态解析器消费的刷新请求；并非
`account_role_item_effects` 未保存、字段投影错误或 `2/10` 回包少字段。当前项目的边界禁止
修改 CBE/CBM、客户内存或回调，也禁止服务端自行推送或借用无关 `2/10`/`25/7` 回调。故服务端
无法在保持真实客户端协议的前提下令“悟”即时出现；重登时的 `1/1/6` 是目前已证实的唯一
合法更新入口。若以后取得原版客户端在道具确认后发出的状态刷新请求/事件证据，才可为该
确切请求实现响应并解除这一限制。

## `ida_multi_mcp` 复核：状态栏缓存链与 `2/10` 契约（2026-08-27）

本轮通过当前打开的 `江湖OL.CBE` IDA 实例完成只读复核；同一 MCP 会话还列出了
`mmGameMstarWqvga.cbm`、`mmShopMstarWqvga.cbm`、`mmTitleMstarWqvga.cbm` 与
`mmBattleMstarWqvga.cbm`。`mmTitleMstarWqvga.cbm:0x53EC`
`role_manage_screen_handle_network()` 的 kind-1/subtype-6 分支只转入标题侧的
`mmorpg_LoginRecord` 路径，不能说明场景内 `25/7` 回调会取得通用角色状态 parser。

### CBE parser／状态栏证据

- `江湖OL.CBE:0x0102DCDA` 的完整反编译确认其只接受 `1/25/7`，读取 `result` 与
  `useinfo`；成功分支只清除等待和数量窗口、复制文字并显示消息框。它没有读取
  `expbook`、没有构造新的网络请求，也没有调用通用 kind-1 状态解析。
- `expbook` 字符串 `0x0101353C` 在 CBE 中只有 `0x010133CC` 一处 data xref。该处的
  实际指令是 `ADR R1,"expbook"`、`BLX` 字段 accessor，随后
  `STRB R0,[R1,#5]`；紧邻的 `expcard` 读取写入同一状态记录的 `+4`。这仍是
  `scene_runtime_init_and_sync(0x01012FB4)` 的 kind-1/subtype-6 状态数据路径，
  而非 item-use 回调路径。
- `muse.gif` 只在 `0x01043B84` 被静态 UI 文件名表
  `scene_ui_tile_catalog_build_filename_table()` 引用。实际每帧图标更新由
  `scene_runtime_tick:0x01015212` 的 `BL UpdateSceneMenuState` 完成；
  `UpdateSceneMenuState(0x0100E3E2)` 在缓存字节为 `1` 时启用对应槽并选择资源号
  `94`（`muse.gif`）。因此先前把 `0x01013594` 称为“状态栏重建函数”并不精确：它是
  scene 初始化函数内的 PC；真正的显示消费点是 `0x01015212 → 0x0100E3E2`。

这条链表明固件具备立即绘制“悟”的 UI 支持，但前提是先经 subtype-6 把 `expbook` 写入
自己的状态缓存；它不会从 `25/7.useinfo` 推导该状态。

### 成功确认后的真实请求／响应证据

`bin/server_out.txt` 中一次成功确认记录为：

```text
mock_battle_insight_followup request=25/7 seq=63 num=2 response=68 result=1 action=commit-after-confirm
net_send wt=25/7 len=31 source=builtin-battle-insight-followup resp=68
net_send wt=2/10 len=19 source=builtin-actor-other-only10 resp=40
```

`bin/multiplayer-data/player-3/logs/battle-insight-status-trace.log` 的对应窗口也记录：

```text
queued-25-7 ... bytes=68 ... generic_1_1=0 subtype6=0 expbook=0 status_rebuild=0
queued-2-10 ... bytes=40 ... generic_1_1=0 subtype6=0 expbook=0 status_rebuild=0
```

对该真实后续请求的 IDA 复核给出完整字段契约：

- `net_handle_actor_move_info(0x01012DD8)` 的 case 10 调用
  `ParseSceneOtherNodeData(0x01012964)`。
- 后者先读取 `othernum`；仅当它大于零时才读取 `otherinfo`，并把流中的 actor id、
  坐标、外观和名称交给 `scene_node_find_or_create()`。它没有读取或写入当前角色的
  `expbook`、`expcard`、`ruffianflag` 或状态栏缓存。
- 当前 `vm_net_mock_build_actor_other_only10_response()` 的窄回包是
  `1/2/10 {othernum:u32=0, otherinfo:empty}`，位于
  `src/server/mock_server_social.c`；`mock_server_dispatch.c` 只在该请求匹配时将其标为
  `builtin-actor-other-only10`。这与 CBE 的零 actor 分支精确一致，不能把角色时效字段
  塞进该对象。

### 结论与下一步边界

这次结论不是基于“重登后可显示”的推测，而是由成功后的请求字节、正常 event-7 回调窗口、
`25/7` parser、`2/10` parser、`expbook` 唯一 xref 和每帧状态栏读取链共同给出：当前客户端在
确认战斗心得后没有发出能驱动 subtype-6 当前角色状态更新的请求，也没有登记可由服务端通过
普通 event-7 交付的此类回调。故本轮不修改 `src/server/`；为 `2/10` 附加 `expbook`、在
`25/7` 拼接 `1/1/6` 或服务端伪造推送都会违反已证实的 parser／callback 契约。

若要解除限制，新的准入证据必须同时包含：确认后的原始出站 WT 请求（或固件已登记的异步
event）、其 callback/context、能读取当前角色 `expbook` 的 IDA parser 分支，以及最小成功
响应对象/字段。取得这四项后，才可在对应的精确 detector 前添加普通 event-7 handler。

## CBM 交叉复核：战斗模块的平行 `25/7` 回调（2026-08-27）

本轮也通过 `ida_multi_mcp` 对当前打开的 `mmGameMstarWqvga.cbm`、
`mmTitleMstarWqvga.cbm`、`mmBattleMstarWqvga.cbm` 与 `mmShopMstarWqvga.cbm` 做了
只读字符串和 xref 检索。四个 CBM 均不存在 `expbook` 或 `muse.gif`，因此场景左上角“悟”
图标的字段消费／资源选择仍归 `江湖OL.CBE`，不能由 CBM 直接补写。

不过 `mmBattleMstarWqvga.cbm` 有一套平行的战斗内物品回调：

- `mmBattle:0xA312` 同时处理 `1/25/6` 与 `1/25/7`。前者读取
  `result/maxnum/iteminfo`；后者读取 `result/useinfo`，并在成功时额外读取 `hour`、`min`。
- 该成功分支调用 `mmBattle:0x9C6E`。该函数以立即数 `828` 遍历战斗模块自己的物品效果
  链表，重算 828 的条目数和累计时长；它不读取 `expbook`，不引用 `muse.gif`，也不调用
  CBE 的 scene 状态 parser 或状态栏更新函数。
- 当前 player-3 的实际确认 trace 记录的是 `25/7` event-7 回调窗口，且没有进入通用
  subtype-6/`expbook` 路径；它没有任何 `mmBattle:0xA312` 命中证据。结合已确认的场景
  `25/7` parser `江湖OL.CBE:0x0102DCDA`，不能因请求头同为 `1/25/7` 就向当前场景路径
  附加 `hour/min`，或把 mmBattle 的本地计时更新误作“悟”图标刷新；宿主也不得据此
  替换／选择 callback。

其余 CBM 命中同样不构成当前契约：`mmGame` 的 `iteminfo` 分别属于 type 字段驱动的物品
表读取与 `17/1` 物品列表读取；`mmTitle` 没有这些时效字段；`mmShop` 只读取
`ruffianflag/type`，解释了它有独立的“力”相关 UI 路径，但不提供 `expbook` 的等价入口。

因此仍不修改服务端：若以后要支持“战斗中”使用心得的 mmBattle 专用显示，必须另行捕获该
屏幕登记 `mmBattle:0xA312` callback 的真实请求、回包和所需的 `hour/min` 语义；它与本问题中
场景左上角“悟”的即时刷新是两个独立契约。

## 场景已登记 event-7 处理表的穷举复核（2026-08-27）

针对“完整网游固件不应只剩登录刷新入口”的疑问，本轮没有再从道具回包猜字段，而是从场景
运行时实际登记给网络层的回调反向逐项检查。`scene_runtime_init_and_sync()` 在
`江湖OL.CBE:0x01012FB6..0x01012FC2` 将
`net_business_response_dispatch(0x01012E4C)` 写入当前网络对象的 callback 槽；宿主随后仍只把
CBE 已登记的 callback/context 和不透明 WT 字节以 event `7` 投递。

该 dispatcher 解包共享的 88 字节 entry 后，顶层 `kind` 的已登记分支为：

| 顶层 kind | 场景分支 | 与“悟”缓存的结论 |
| --- | --- | --- |
| `2` | actor/move（含 subtype `10`） | `10` 只进入 `ParseSceneOtherNodeData(0x01012964)`。它读取 `othernum`，再按固定流顺序读取 actor id、格坐标、外观、名称、目标坐标／名称并调用 `scene_node_find_or_create()`；没有当前角色时效字段。 |
| `7` | misc player fields | 已有实时字段仅包括 `pcimg`（`20`）、`expinfo`（`31`）和 `expcard`（`32`），另有 `36/37/38/41/42` 的说明、获得物品等分支；没有读取 `expbook` 的 subtype。末尾的物品操作子分支只处理 subtype `1/4/8/9`。 |
| `22` | ruffian 更新 | subtype `5/6` 读取 `result`、`ruffianflag`、`info`，写入角色的独立 ruffian 状态。这正是“大力丸后立即显示力”的真实契约，不能复用为心得。 |
| `28` | 场景跳转 | 只接受跳转结果／`scene`／`posinfo` 或带 `second` 的提示；走同场景跳转会启动位置／场景生命周期，不能充当时效刷新。 |
| `30` | scene-channel | 包括 scene/posinfo、NPC／房间角色表、任务文本、`role_stat` 等；`role_stat` 仅更新坐标、energy、energymax、gold。 |
| 其余已登记 kind | 登录、队伍、任务、战斗技能、交易、称号、消息、活动等 | 都没有 `expbook` 字段引用。event-7 分派完成后还会检查 kind `12/4/6/27` 的后续事件；其中没有当前角色的心得标志。 |

最关键的反证是：该实际场景 dispatcher 的 29 路跳表没有顶层 `kind=1`。因而把
`1/1/6` 拼入任何正在由该 callback 接收的正常 event-7 WT 帧，虽然会被共同解包到 entry
容器，也不会转入 `scene_runtime_init_and_sync()` 的 fresh-enter 扫描。此前玩家 trace 中
`generic_1_1=0` 与此完全相符，而不是服务端遗漏了某个 `2/10` 字段。

`expbook` 的唯一字符串读取仍是 fresh-enter 中 kind-1/subtype-6 的
`0x010133CC`，其紧邻指令把返回字节写到场景时效缓存 `+5`；`UpdateSceneMenuState()` 才消费
这个缓存并选取 `muse.gif`。这条路径与上表所有正常场景 event-7 handler 均不相交。

因此，当前可见的实时状态方式分别是 `7/20.pcimg`（修）、`7/32.expcard`（练）、
`22/5|6.ruffianflag`（力）和 fresh-enter 的 `1/1/6.expbook`（悟）。前三者是不同图标的
既有字段，第四者属于 fresh-enter 生命周期且当前场景 dispatcher 不分派。伪造
scene/posinfo 跳转、把自身塞入 `2/10.otherinfo`，或借用 `7/20`／`7/32` 均不会让 CBE
读取 `expbook`，并会违反各自已经确认的对象契约；所以本轮没有新增 `src/server/` handler。
若要继续寻找原版在线协议差异，应取得原版在 828 确认后额外发出的 WT 或服务器主动 event 的
原始字节及其登记 callback；在这之前，服务端不应猜测一个“补洞”对象。

## 从“修”图标反查的同构实时状态路径（2026-08-27）

“修”是同一个 `UpdateSceneMenuState(0x0100E3E2)` 中的资源 `ep_icon.gif`（资源号 `54`）。
它并不依赖场景重登：函数读取缓存 `global+31273`，该值为 `0` 时启用资源 54、为 `1` 时隐藏。
同一函数紧邻的三个槽位是 `training.gif`（资源 53，练）、`ruffian.gif`（55，力）和
`muse.gif`（94，悟），读取的缓存依次为 `+31308`、角色 `+1135` 和 `+31309`。

这给出了可检验的同构反查，而不是仅按资源名推测：

| 图标 | 正常 event-7 parser | 写入字段 | 已捕获的请求／回包 |
| --- | --- | --- | --- |
| 修 | `net_handle_misc_player_fields(0x01011C88)` subtype `20`，及标准物品使用完成分支 | `pcimg` -> `global+31273` | `1/7/7 {type=2}` -> `1/7/20 {result,pcimg}` |
| 练 | 同一函数 subtype `32` | `expcard` -> `global+31308` | `1/7/7 {type=3}` -> `1/7/32 {result,expcard}`；活动经验卡还会由 `SendSceneAction31()` 发 `1/7/31` 请求以取得 `expinfo`。 |
| 力 | `net_handle_ruffianflag_info(0x01010F6C)` subtype `5/6` | `ruffianflag` -> 角色 `+1135` | 时效攻防道具的 `1/22/3` 成功回包。 |
| 悟 | 没有当前场景 event-7 同构分支 | 只有 fresh-enter `1/1/6.expbook` -> `global+31309` | 828 的 `1/25/7` 后只捕获到无关的 `1/2/10`。 |

原始登录抓包进一步固定前两项：
`login-equipment-packet-capture/run-00015983-00046708-001/004-uplink.wt` 为
`1/7/7 {type=2}`，`005-uplink.wt` 为 `{type=3}`；随后 `007-downlink.wt` 是
`1/7/20 {result=1,pcimg=0}`，`008-downlink.wt` 是
`1/7/32 {result=1,expcard=0}`。这证明固件在需要时确实会登记并使用“单字段实时徽标”
协议；`net_handle_misc_player_fields()` 的逐条比较也证明其中没有漏标注的 subtype 会读取
`expbook`。特别地，`SendSceneAction31()` 只在缓存 `expcard==1` 时发送 `1/7/31`，没有读取
`expbook` 的对应发送器。

所以从“修”附近反查没有发现可复用的隐藏路径，反而将差异缩小为确定事实：828 的
`ProcessItemUseResult(0x0102DCDA)` 与已有修／练／力完成 handler 不同，它不写任何徽标缓存，
也不发状态查询。不能把 `pcimg` 或 `expcard` 改名为 `expbook`，因为 parser 访问的字段名、
目标缓存字节和图标资源都各不相同。

## 已实施的真实实时徽标修正（2026-08-27）

基于上述原始 `1/7/7` 往返字节与 CBE subtype-20／32 parser，服务端仅收束了已经存在的
实时徽标契约：`src/server/mock_server_interaction_login.c` 不再为 type-2 查询硬编码
`pcimg=0`，而是回传无固定“修”标识的 `pcimg=1`。这是对 `1/7/20` 的字段值修正，不是为
828 伪造新事件；固件仍以既登记的 event-7 callback 收到原样 WT 响应。

为避免未来重新引入已证伪的方案，也移除了 `src/server/mock_server_catalog.c` 中从未接入生产
dispatcher 的静态 `1/1/6` 拼包 helper，并将
`timed-item-status-icon-regression` 改为断言实际 `1/7/7 {type=2}` ->
`1/7/20 {result=1,pcimg=1}` 与 `{type=3}` -> `1/7/32 {result=1,expcard=0}`。已通过
`make -j2`、`timed-item-status-icon-regression`、`battle-insight-followup-regression` 和
`battle-insight-status-regression`。这完善了修／练／力的可验证实时协议边界；“悟”仍无对应的
已登记 event-7 parser，因此没有给 `25/7`、`2/10` 或任何现有对象附加 `expbook`。
