# 周围玩家操作菜单（2026-07-11）

phase: nearby-player-actions
status: implemented

## 请求与客户端路径

`江湖OL.CBE:0x0103023C` 在周围玩家列表的二级操作菜单中按选项索引调用下列发送器。这里的请求均为一个 `major=1` 的 WT 对象，目标字段为 `id`。

| 菜单操作 | 请求 | 额外字段 | 发送器 / 证据 |
| --- | --- | --- | --- |
| 玩家信息 | `10/2` | `type:u8=2` | `0x0101A5AA` `SendShopType2Event` |
| 查看装备 | `29/4` | — | `0x0101A2B6` |
| 入帮邀请 | `10/13` | `fid` | `0x0101A3C8`; 字符串 `0x0101A74C` 为“入帮邀请已发送” |
| 交易请求 | `21/1` | — | `0x0101C408` |
| 组队邀请 | `5/1` | — | `0x0101BEF4` |
| 发起私聊 | 无网络请求 | — | `0x0101621C` 只调度本地私聊界面转换 |
| 切磋请求 | `4/14` | — | `0x0102E624` |
| 加为好友 | `10/3` | — | `0x0101A2EA` |

运行时对请求的目标约束为：`id` 必须仍能解析为当前场景中另一个已可见服务会话；处理器不会吞掉任意同头 WT 包。

## 已实现下行

### 玩家信息 `10/2`

response:
  wt_kind: 10
  wt_subtype: 2
  fields: `result:u8=1`, `type:u8=2`, `fdegree:GBK-string`, `num:u8=2`, `playerinfo:two serial strings`

ida_evidence:
  binary: 江湖OL.CBE
  function: `HandleFactionDegreeResponse`, `0x010211A8`
  parser_reads: `result`, `type`, `fdegree`, `num`, `playerinfo`

`playerinfo` 使用客户端序列阅读器所需的 16 位长度前缀字符串。客户端按 `playerinfo` 的 `num` 条目依次显示，随后固定追加 `fdegree`；当前响应采用三行 GBK 中文排版：`姓名：<名称>`、`等级：<等级>级　职业：<战士/刺客/法师>`、`帮派：<真实帮派名/无帮派>　性别：<男/女>`。固定标签使用显式 GBK 字节，目标角色名保留其角色数据库中的 GBK 显示名；帮派名由目标在线会话的账号与角色 ID 查询 `guild_members`，不再写死“暂无”。

### 查看装备 `29/4`

response:
  wt_kind: 29
  wt_subtype: 4
  fields: `result:u8=1`, `num:u8>=1`, `level:u16`, `name:string`, `work:u8(jobIndex 0..2)`, `sex:u8(1-based 1..2)`, `equipinfo:raw`

ida_evidence:
  binary: 江湖OL.CBE
  function: `HandleEquipInfoResponse`, `0x010216D6`; equipment view renderer `0x01020C1A -> 0x010206C4`
  parser_reads: `result`, `num`, `level`, `name`, `work`, `sex`, `equipinfo`; renderer indexes resource table with `2*work + sex`

`equipinfo` 没有内置行数，行数由外层 `num` 指定；每一行是 `u32 itemId, u16 seq, i16 runtime, i16 reserved, u8 attr_count=0`。服务端从目标在线会话捕获的实际装备槽位生成行，并只发送本地 `equip.dsh` 已知的 id。若会话尚未完成升级后的在线状态捕获，或请求来自离线好友行，则回退到目标职业的默认武器（`1001`、`1501` 或 `2001`）。

目标 `id` 的授权范围是当前场景中可见的其他角色，或持久化好友表中属于当前角色的好友行；后者覆盖好友列表上的“查看装备”请求，避免将任意 `29/4` 包泛化为可查询所有角色。

### 好友邀请 `10/3 -> 10/4 -> 10/5 -> 10/6`

好友请求不再在发起时直接写入好友表。服务端先向目标角色排队一条下行邀请；目标客户端在下一次正常场景同步轮询中收到：

| 阶段 | WT | 字段 | 客户端证据 |
| --- | --- | --- | --- |
| 发起 | `10/3` | `id` | `0x0101A2EA` |
| 目标邀请 | `10/4` | `id`, `name` | `0x010114FC`；显示“邀请加你为好友”并安装确认回调 |
| 目标回复 | `10/5` | `id`, `result:u8`（`1` 同意，`2` 拒绝） | `0x010114A4` |
| 发起方结果 | `10/6` | `result:u8`, `name` | `0x010114FC`；显示同意或拒绝结果 |

只有目标回复 `result=1` 后，服务端才将双向好友关系写入 `nvram/mock_service_friends.bin`。因此好友列表需要先经过真实的目标端确认才会显示新好友。

### 交易请求 `21/1 -> 21/2 -> 21/3 -> 21/4`

| 阶段 | WT | 字段 | 客户端证据 |
| --- | --- | --- | --- |
| 发起 | `21/1` | `id` | `0x0101C408` |
| 目标邀请 | `21/2` | `id`, `name` | `0x01011132`；打开同意/拒绝确认框 |
| 目标回复 | `21/3` | `id`, `result:u8`（`1` 同意，`2` 拒绝） | `0x01011076` |
| 发起方结果 | `21/4` | `result:u8`, `name` | `0x01011132`；同意时进入客户端交易界面，拒绝时显示提示 |

本轮实现的是邀请与确认的跨客户端投递；具体交易物品、锁定和结算的后续业务包仍未实现。

### 投递机制

远程 mock service 是短连接的请求/响应模型，无法对空闲 CBE 客户端主动写 socket。每个在线客户端已有 event-7 场景同步轮询，因此服务端将待投递的好友/交易通知作为正常 WT 对象附在该轮询响应中；每次只投递一个通知，且已有未回复的模态邀请时不会叠加新的邀请框。

### 组队 `5/1 -> 5/2 -> 5/3 -> 5/4 -> 5/5`（`5/10` 全量刷新）

| 阶段 | WT | 字段 | 客户端证据 |
| --- | --- | --- | --- |
| 发起 | `5/1` | `id` | `CheckBattleJoinCondition(0x0101BEF4)` |
| 目标邀请 | `5/2` | `id`, `name` | `net_handle_group_info(0x01011F3A)`；弹出确认框 |
| 目标回复 | `5/3` | `id`, `result:u8`（`1` 同意，`0` 拒绝） | `HandleGuildJoinConfirm(0x01011ED0)`；名称虽旧，实际发送的是队伍回复 |
| 发起方结果 | `5/4` | `id`, `result`, `name` | `net_handle_group_info` subtype 4 |
| 新成员增量 | `5/5` | `groupinfo` 单行 | `net_handle_group_info` subtype 5 |
| 全量成员表 | `5/3`（接受方直回）或 `5/10`（轮询） | `result=1`, `num`, `groupinfo`; `5/10` 另有 `leadid` | `net_handle_group_info` subtypes 3/10 |

服务端仅在目标 `result=1` 时创建/扩展在线临时队伍；队长在索引 0，最多三人，非队长不能继续邀请。

2026-07-15 静态证据修正：成功的 `5/3` 不能只有 `result`。`net_handle_group_info(0x01011F3A)` 的 subtype 3 和 subtype 10 共用 `0x0101216A` 的全量成员解析器；subtype 3 在 `result == 1` 时从 `0x0101217E` 继续读取 `num`、`groupinfo`，循环至 `0x01012224 AddRoleToList`，并把首行 id 写成队长 id。仅返回 `5/3 { result:1 }` 属于截断的成功包，客户端不会建立完整队伍模型，表现为加入成功但没有队伍 UI。成员容器也不是由该结果包临时创建：`scene_system_bootstrap(0x01003856)` 已在 `0x01003904-0x0100390C` 调用 `AllocBufIfNull(Global_R9+0x5CF4, 0x130)`，因此正常进入场景后容器应先于邀请存在。

加入时采用客户端原生的全量/增量分工：接受方的请求直回为单个完整 `5/3 { result=1, num, groupinfo }`；已有的其他队员只收到 `5/5` 新成员行。2026-07-16 的好友列表邀请复测进一步证明，邀请方不能只收到 `5/4 + 5/5`：`net_handle_group_info` subtype 4 只显示同意提示，subtype 5 只追加远端一行，而好友列表发起方此前没有本机 roster 行，最终成员计数只有 1。聊天/队伍入口 `HandleSceneMenuNavigation(0x0101AED2)` 在 `0x0101B182` 明确要求 `Global_R9+23668 > 1`，所以服务端队伍虽然已有两人，队长端仍不会出现 UI。现在邀请方在 `5/4` 后收到完整 `5/10 { result=1, num, groupinfo, leadid }`，一次建立队长和新成员两行并设置队长 id；已有第三方队员仍使用 `5/5` 增量。早先 `resp=148` 的 `0x01011E3A` 崩溃并不是全量通知时序本身导致，而是当时 `groupinfo` 中多插入的伪 reserved/tagged-id 字节；当前全量 builder 已采用验证后的 raw-first-id 布局。

好友列表的 `5/1.id` 是持久角色 id，目标不一定与邀请方处于同一场景。组队邀请目标解析现在先匹配同场景观察端 actor id，再用已确认好友关系的持久 role id 解析在线 session；第二条路径复用装备查看已验证的 friend-scope resolver，不把任意 role id 扩大成可邀请目标。关键日志新增 `mock_team_invite ... scope=nearby|friend`，队长成功刷新为 `team_result_deliver ... roster_update=5/10-full members=2`。

2026-07-11 runtime negative evidence: 两个游客账户均为持久 roleId `10001` 时，周围玩家节点使用客户端作用域的 `0x6Axxxxxx` actor id 避免冲突。`5/1.id` 因而是该节点 id，而不是目标 `onlineRoleId`。邀请处理器先以这个 actor id 精确解析 nearby seed；之后不得再次比较 `onlineRoleId == actorId`，否则会把可见目标错误记录为 `target-offline` 并丢弃邀请。队伍管理器同样以成员行的 role id 去重；故 `5/3`、`5/10`、`5/11` 及发给同队他人的 `5/7` 均必须按观察端编码：本机仍为真实 `10001`，冲突的远端成员为该观察端已使用的同一个 `0x6Axxxxxx`。不能把两行都编码为 `10001`，否则客户端只保留一个成员。

同一约束也适用于邀请确认本身：`5/2.id` 必须是确认端所见的远端 actor id，不能是与确认端自身相同的持久 `10001`。该 id 会被客户端原样带入 `1/5/3.id`；服务端以投递时保存的 `sourceClientId + sourceWireId` 关联邀请方，不再把该字段误判为持久 roleId。2026-07-11 的负面运行证据是日志只到 `social_notice_deliver ... action=team-invite`，未出现 `mock_team_invite_reply`；这与确认包携带本机冲突 id 相符。若确认端仍发出过期或不匹配的 `1/5/3`，服务端会以 `mock_team_invite_reply_reject` 记录收到的 id、挂起状态和期望 id，而不是落入未处理断言。

`groupinfo` 的完整行不是 otherinfo，也不是装备行。这里有一个容易遗漏的对象层/序列层叠加：`vm_net_mock_put_object_blob` 会在实际 blob 数据前加入 `len16`；客户端字段访问器保留这个前缀，`stream_read_i32_be_tagged` 又无条件跳过当前两字节。因此 blob 第一行必须是 `raw-be32 roleId, len16 名字, tagged-u8 sexGroup(1..2), tagged-u8 jobIndex(0..2), tagged-u8 online, tagged-u32 hp, mp, hpmax, mpmax`；若全量表还有后续行，后续行 id 才显式写成 `tagged-u32`。成员更新 subtype 5 是单行 blob，第一项 id 同样是 raw BE32，并只比完整行少 `online`。subtype 11 的单行 `hsp` 则有意采用不同顺序：`raw-be32 roleId` 加四个 tagged 大端 `u32`：`hp, hpmax, mp, mpmax`。关键点是 id 与名字之间没有 reserved 字节。`stream_reader_init_from_blob(0x01033B16)` 把 reader `+0x2C` 安装为 `stream_peek_i16_be(0x01033A1E)`：它只查看当前游标处的两字节名字长度，不推进游标；随后 reader `+0x1C` 的 `stream_read_cstr_len16(0x01033A86)` 才消费同一长度和名字。完整成员表在 `0x010121A0`、subtype 5 在 `0x0101209E` 都遵循 `id -> peek(nameLen) -> name -> sexGroup -> jobIndex`。

两个视觉字节的语义由后续数据流确认：它们在 `0x01012224` / `0x01012116` 传入 `AddRoleToList`，后者于 `0x01011E86` 以 `(jobIndex, sexGroup)` 调用 `GetMapTileData(0x01011E08)`。该函数从当前角色对象开头的六个资源指针中读取 `2 * jobIndex + sexGroup - 1`；队伍 HUD 在 `0x01014368-0x01014388` 对 44 字节绘制对象使用相同索引。因而它与 title/actorinfo/装备界面一致，必须把数据库 `job=1..3, sex=0..1` 归一化为 `jobIndex=0..2, sexGroup=1..2`。

2026-07-15 的连续负面运行证据先锁定了行游标：插入伪 reserved `00 01 00` 后，peek 把它的 `00 01` 当成“名字长度 1”，字符串 reader 把最后的 `00` 当成空名字，真实名字及后续字段全部错位。完整 `5/3` 的 `resp=148` 随后在 `AddRoleToList(0x01011E3A)` 看到 `Global_R9+0x5CF4 = 0x04000101`；该数值逐字节正是错位流中的 `01 01 00 04`（`online=1` 的尾部与下一个 tagged-u32 头拼接）。删除伪 reserved 后名字和四个属性已重新对齐，但视觉值一度仍错误地发送数据库原值。

2026-07-15 后续复测仍报告 `resp=148` 和 `0x01014388 -> PC=0`，但进程核对发现两个 mock-service 都仍加载 19:05 的旧 `bin/main.exe`，而修正后的 `obj/main.o` 是 19:43；旧进程还阻止链接器替换可执行文件。对于同一份两成员响应，删除每行一个 tagged-u8 伪 reserved 会使包长从 `148` 减少 6 字节，因此这次 `resp=148` 不是修正布局的新负面证据。正式重新链接后，队伍 builder 增加携带 blob 长度、视觉值和首行编码方式的 `mock_team_groupinfo` / `mock_team_member_join` 日志，用于直接确认实际运行版本。

新版运行证据随后把第二层 builder 错误单独暴露出来：接受方完整 `5/3` 为 `resp=142, groupinfo_len=96`；邀请方轮询收到 `5/4 + 5/5` 合包 `resp=118`，builder 打印的增量行是 `sex=0, job=3, groupinfo_len=47`，随即在 `0x01014388` 以空回调崩溃。对该成员正确的线上视觉值是 `sexGroup=1, jobIndex=2`；builder 日志因此改为显式打印 `sex_group` 与 `job_index`。后续对象转储又证明这并非当时唯一剩余的错位：`groupinfo_len=47` 仍包含多出的第一项 `00 04`，正确单行应再减少 2 字节。

同一崩溃地址也可能由非组队业务错误注入 roster：2026-07-15 的 `resp=248` 来自 `builtin-challenge-interaction`，其中旧的战斗预填逻辑在 `4/1` 前附加了一个 `5/5` 本机角色模板。即使用户尚未组队，主 CBE 仍会把它当成增量队员；战斗切换尚未完成时，场景 HUD 随后绘制该临时行并在 `0x01014388` 调用空资源回调。随后把本机模板移到登录 `5/10` 的 solo roster 仍然错误：新运行日志显示客户端尚未进入场景就收到 `resp=232`，用户转储中 roster 节点 `+36` 是 `0x00040000` 而不是 `10001`，`+2` 则从 `00 05 <名字>` 开始。该字节形状精确证明第一行 id 被重复写了 tagged 头：blob 自带前缀已被当作头，额外的 `00 04` 被解析成 id 高字节，真实 `10001` 又被误当名字长度，最终覆盖节点并在 `0x01014388` 调空回调。未组队的 `5/10` 因此恢复为空表；战斗响应也不默认夹带任何 `5/5`。

离队处理器接收 `1/5/6` 并回送/广播 `5/7 { id }`。普通成员离开只从其余客户端删除该行；队长离开时客户端对 leader id 的既有分支会清空整队，因此服务端同步解散该临时队伍。角色断线也走同一 `5/7` 清理流程，不保留离线幽灵队员。

2026-07-15 HP/MP 运行验证补全：战斗 `4/2` builder 使用独立的
`g_mockBattleRoleHpCurrent/g_mockBattleRoleMpCurrent` 推进回合状态，旧实现只在终局
结算时把 HP 写回角色，导致普通回合后的会话捕获仍看见旧 HP。现在每个成功的战斗
操作、战斗道具和逃跑响应都会先把当前 battle vitals 发布到活动角色，再由既有的
post-request presence capture 检测变化并排队 subtype `5/11`。该更新广播给队伍内
所有客户端（包含数值发生变化的本机），因为队伍 roster 节点与战斗角色缓存是两个
独立的客户端状态。`5/3`、`5/5`、`5/11` 的成员行也把 `HP=0` 保留为合法死亡值，
不再以“未设置”为由替换成 `hpmax`。

隔离双会话证据保存在 `tmp/team-hsp-latency-validation/service.log`。两名同 roleId
`10001` 的玩家完成 `5/1 -> 5/2 -> 5/3` 后，玩家 A 使用技能使 MP 从 `100` 降为
`90`；服务端随后分别向 A 与 B 投递 `resp=47` 的 `5/11`，日志中的本机 wire id
为 `10001`、远端 wire id 为观察端对应的 `0x6Axxxxxx`，两条均为
`hp=99/120 mp=90/100`。

2026-07-15 再次逐指令核对发现初始成员行与 `5/11` 的统计量线序并不相同。
subtype 3/10 在 `0x010121E4-0x01012224`、subtype 5 在
`0x010120DA-0x01012116` 都按线序读取四项后反向安排 `AddRoleToList` 参数；结合
`AddRoleToList(0x01011E68-0x01011E74)` 的节点写入，初始行应为
`hp, mp, hpmax, mpmax`，分别落到节点 `+40,+44,+48,+52`。HUD
`scene_draw_team_member_status_list(0x01014168)` 在 `0x010142B0-0x01014348`
明确使用 `+40/+48` 绘制 HP、`+44/+52` 绘制 MP。旧 builder 把初始行误发成
`hp,hpmax,mp,mpmax`，会让 HP 上限读成当前 MP、当前 MP 读成 HP 上限；现在全量
`5/3、5/10` 与增量 `5/5` 都已改为正确线序，`5/11` 保持
`hp,hpmax,mp,mpmax` 不变。

同一轮还确认了明显卡顿不是 WT 业务耗时。通过 PowerShell
`Start-Process -RedirectStandardOutput` 启动服务时，程序入口的 `_IONBF` 会让每条
日志经 .NET 重定向管道同步阻塞约 `90-220 ms`；一个 `2/1` 移动请求打印多行，累计
`process_ms=604-854`。保持同一可执行文件和请求，只改为 `cmd.exe` 的原生
`> service.log 2> service.err` 文件重定向后，登录请求耗时 `20 ms`，连续两个移动
请求分别为 `3 ms` 与 `2 ms`，服务端内部 `process_ms=4/2`。因此运行脚本必须使用
操作系统直接文件重定向，不能把高频服务日志接到 PowerShell 的异步重定向管道。

当前“组队战斗”覆盖的是客户端已证明的队伍生命/法力状态同步和战斗前角色模板复用；跨客户端共用一个回合、让另一台 CBE 在未请求的情况下进入同一场战斗，尚没有 `4/x` 入场及回合归属的客户端证据，未发送猜测的战斗开始包。

### 组队 UI 接受阶段证据记录（2026-07-15）

phase: team-invite-accept-ui
status: implemented

request:
  wt_kind: 5
  wt_subtype: 3
  objects: `1/5/3`
  key_fields: `id`, `result=1`

response:
  accepted_target: `1/5/3 { result=1, num, groupinfo }`
  inviter_poll: `1/5/4 { id, result=1, name }` followed by `1/5/5 { groupinfo }`
  existing_peer_poll: `1/5/5 { groupinfo }`
  full_refresh: `1/5/10` only in response to a client `WT 5/10` request

ida_evidence:
  binary: 江湖OL.CBE
  function: `net_handle_group_info(0x01011F3A)`
  dispatch_case: subtype 3/10 at `0x0101216A`; subtype 5 at `0x0101208C`
  parser_reads: successful subtype 3 reads `result,num,groupinfo`; subtype 5 reads one incremental row; blob prefix supplies the first id reader's two-byte header, so first-row id is raw BE32, later row ids are tagged; then `name,sexGroup(1..2),jobIndex(0..2),[online],hp,mp,hpmax,mpmax` with no reserved byte; subtype 11 independently reads `id,hp,hpmax,mp,mpmax`
  name_length: reader `+0x2C` is non-advancing `stream_peek_i16_be(0x01033A1E)`; reader `+0x1C` then consumes the same len16 string
  visual_lookup: `AddRoleToList(0x01011E1E)` calls `GetMapTileData(0x01011E08)` with `(jobIndex,sexGroup)`; `scene_draw_team_member_status_list(0x01014168)` uses the same `2*jobIndex+sexGroup-1` six-slot calculation and calls its draw callback at `0x01014388`
  failure_branch: result-only subtype 3 reaches the full-roster reads without the required fields
  lifecycle: `scene_system_bootstrap(0x01003856)` allocates the `Global_R9+0x5CF4` roster container at `0x01003904`

negative_evidence:
  missing_or_bad_field: successful `5/3` omitted `num/groupinfo`; unsolicited full `5/10` was used where subtype 5 is the native join update; team rows inserted a nonexistent reserved byte, then sent unnormalized database sex/job values, then duplicated the first id's tagged header despite the blob prefix already supplying it
  observed_failure: the extra reserved byte produced `resp=148` and `0x01011E3A` with pointer-like packet bytes `0x04000101`; unnormalized `5/5 sex=0,job=3` produced `resp=118`; duplicated first-id header produced a pre-team login `5/10 resp=232`, roster id `0x00040000`, copied bytes beginning `00 05 <name>`, and a null HUD callback at `0x01014388`

unknowns:
  - name: multi-client shared battle turn ownership
    current_value: not implemented
    why_kept: no confirmed `4/x` client entry/turn contract yet

### 其余邀请与请求

入帮、切磋发送器仍会在发起端立即显示“已发送”提示，服务端目前仅返回空 WT 传输确认。它们的目标端入站通知与接受/拒绝协议尚未根据客户端证据实现；不会伪造帮派关系或切磋战斗包。

negative_evidence:
  missing_or_bad_field: `29/4` 缺少 `equipinfo` 时，`HandleEquipInfoResponse` 走“查看玩家装备信息异常”分支；`result=1` 配合 `num=0`/零行流会让装备查看状态半初始化。`work` 若错误写为持久化职业值 `1..3`，职业显示会错位；职业 3 更会使 `2*work+sex` 越过六项角色资源表，随后在 `DrawMapTileLayer(0x01004E9C)` 访问无效地图层并闪退。
  observed_failure: 先前把菜单动作当作 `2/7` 的变种会导致未处理 WT；这些动作是独立的业务头。好友列表的 `29/4` 目标 id 不一定仍在当前 nearby seed 中，若只按 nearby 解析会落入未处理断言。

unknowns:
  - name: 入帮邀请、切磋请求的目标端入站通知与接受/拒绝包
    current_value: 未实现
    why_kept: 当前发送端没有相应下行解析契约；伪造成功包会跳过真实目标端流程。
