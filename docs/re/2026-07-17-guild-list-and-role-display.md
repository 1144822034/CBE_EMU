# 帮派列表、详情、申请与人物属性（2026-07-17）

phase: guild-list-detail-application-create-leave-recruitment-approval
status: implemented-and-validated

## 问题边界

人物属性页的“帮派”来自登录/场景 `actorinfo` 的第二个展示字符串。旧服务端的
帮派名 helper 无条件返回 GBK“散人”，所以该文字并不是客户端对无帮派状态的推导，
而是服务端硬编码的数据错误。

2026-07-17 后续菜单复核发现，文字只是显示值；真正的成员状态来自角色对象
`actor+104`。`parse_actorinfo_playerinfo_blob(0x0100FA88)` 把首个 ID 写入
`actor+100`（角色 ID），把角色名后的第二个 ID 写入 `actor+104`（帮派 ID）。旧
builder 错误地在两个位置都发送角色 ID，导致所有未入帮角色的 `actor+104` 仍非零。

打开帮派页时的首个运行时边界为：

```text
unhandled wt=10/20 len=37 objects=1 first=1/10/20:28
```

## 客户端请求

ida_evidence:
  binary: `江湖OL.CBE`
  list_sender: `SendGuildPageReq(0x010414DC) -> SendRoleActionEvent(0x0103C830)`
  detail_sender: `SendRoleActionEvent(0x0103C830)`, subtype `22`
  creation_sender: `SendRoleActionEvent(0x0103C830)`, subtypes `30/28/11`
  application_sender: `SendRoleActionEvent(0x0103C830)`, subtype `31`

| 请求 | 字段 | 语义 |
| --- | --- | --- |
| `1/10/20` | `index:u32`, `pagesize:u8` | 帮派列表翻页请求；按一基首行索引读取，首页为 `index=1` |
| `1/10/21` | `index:u32`, `pagesize:u8` | 从帮派菜单首次进入“其他帮派”时的列表请求；返回同一个 `10/21` 列表响应 |
| `1/10/22` | `id:u32` | 重载：`id=帮派 ID` 时读取选中帮派详情；`id=当前角色 ID` 时脱离帮派 |
| `1/10/23` | `id:u32` | 已入帮管理页按当前 `actor+104`（帮派 ID）读取本帮情报 |
| `1/10/20` 或 `1/10/37` | `index:u32`, `pagesize:u8` | 按一基首行索引读取本帮成员；成员首页使用 `10/20`，成员管理分页还会使用 `10/37`；首页为 `index=1` |
| `1/10/30` | 无 | 未入帮角色开始创建帮派；已入帮角色还会复用该 subtype 进入帮派场景路径 |
| `1/10/28` | `name:string` | 校验准备创建的帮派名并进入确认阶段 |
| `1/10/11` | `name:string` | 确认创建并提交最终落库 |
| `1/10/31` | `id:u32` | 向指定帮派提交入帮申请 |
| `1/10/32` | `id:u32=0`, `index:u32`, `pagesize:u8` | 分页读取本帮待审批的入帮申请；首页为 `index=1` |
| `1/10/33` | `type:u8`, `roleid:u32` | 审批申请；`type=1` 批准，`type=2` 拒绝 |

## 列表响应 `10/21`

`HandleFactionMemberListResponse(0x0103F566)` 读取：

```text
result:u8
fid:u32
name:string
allpgs:u16
num:u8
faction:blob
repeat num:
  first row: raw-be32 guildId
  later rows: tagged-u32 guildId
  tagged-u32 guildLevel
  string guildName
  tagged-u32 memberCount
  string leaderName
```

`faction` 字段访问器保留 blob 的 `len16` 前缀，第一个 i32 reader 会消费该前缀，
所以首行 ID 必须是 raw BE32；后续行才使用 tagged-u32。空列表仍返回
`result=1, allpgs=1, num=0` 和有效空 blob；不会返回失败结果或缺失
`faction` 字段。列表名称的客户端行结构只保留 13 字节，服务端数据库因此把帮派名
限制为最多 12 个 GBK 字节，并在发送前再次按 GBK 字符边界截断。

运行时确认客户端首次点击“其他帮派”发送的是 `10/21`，而
`SendGuildPageReq(0x010414DC)` 在翻页/刷新时发送 `10/20`。两种请求都携带相同的
`index/pagesize` 字段，也都由 `HandleFactionMemberListResponse(0x0103F566)` 接收
`10/21` 响应。该解析器进入时立即清除 `screen+145` 的等待标记；若服务端只接受
`10/20`，首次进入请求会落入 `unhandled`，界面便会一直显示进度条。响应中的 `fid`
来自当前角色真实帮派 ID，未入帮时为 0。

2026-07-17 的第二次运行验证还确认，行内 `guildLevel` 和 `memberCount` 虽然最终写入
客户端 16 位显示槽，但 `HandleFactionMemberListResponse(0x0103F566)` 在
`0x0103F7C4`、`0x0103F860` 都通过 `stream_read_i32_be_tagged` 读取，线上编码必须是
`00 04 + BE32`。旧响应错误地使用 `00 02 + BE16`：等级读取会继续吞掉名称的
长度前缀，随后 `0x0103F80E` 把帮派名称的 GBK 字节 `BA DA` 当成字符串长度，并在
`0x0103F828` 的 `mem_copy` 中越界崩溃。该负面证据对应运行时
`LR=0x0103F82D, R7=0xFFFFBADA`。

## 详情响应 `10/23`

`HandleFactionInfoResponse(0x0103F088)` 在 `result=1` 时读取 `faction` blob：

```text
raw-be32 guildId
string guildName
string leaderName
u32 guildLevel
string memberCountText       // 例如 1/20
u32 guildMoney
u32 prosperity
u32 actionPower
u32 researchPower
u32 construction
string currentConstruction
string notice
```

客户端据此生成“帮主、帮派等级、成员数、帮派金钱、繁荣度、行动力、研究力、建设
度、当前建设、帮派公告”各行。详情成功后，服务端仍记录当前选中的 `guildId` 供界面
状态使用；入帮申请不再依赖该隐式状态，而是严格读取 `10/31.id`。

详情同样复用 blob 的 `len16` 作为首个 i32 的前缀，因此 guildId 不能用 tagged-u32。
多发 `00 04` 会把客户端游标整体错开两字节：名称、帮主、等级和成员数均错位，解析出的
ID 也不再等于 `actor+104`，从而把详情页返回状态设置成错误分支。

详情请求存在两个已确认的 subtype：列表页 `10/22.id` 是选中行的 `guildId`；已入帮
管理页 `HandleRoleListInput(0x0103E76A)` 发送 `10/23.id=actor+104`，也是当前
`guildId`。2026-07-17 创建成功后的运行时请求为 `10/23 {id=4}`。该请求曾被更早的
通用 `role_action23` 处理器截获，只返回 `result+id`；客户端
`HandleFactionInfoResponse(0x0103F088)` 在 `result=1` 时继续读取必需的 `faction`
blob，因而显示“数据错误”。帮派详情处理器现在位于该通用 fallback 之前。

## 帮派成员页 `10/20|37 -> 10/20`

ida_evidence:
  binary: `江湖OL.CBE`
  screen_table: `InitScenePageTable(0x01042F32)`
  initial_request_sender: `ResetGuildListAndReq(0x01041502)`, subtype `20`
  paging_request_sender: `SendGuildMemberPageReq(0x0104214E)`, subtype `37`
  initial_response_dispatch: `HandleFactionEquipInfo(0x010420B8)`, subtype `20`
  paging_response_dispatch: `DispatchFactionNetEvents(0x01042CB2)`, subtype `20`
  response_parser: `HandleFactionPlayerListResponse(0x01041D66)`

成员首页与“其他帮派”翻页实际复用了完全相同的请求：

```text
1/10/20 { index:u32, pagesize:u8 }   // 帮派成员首页；也可能是其他帮派翻页
1/10/37 { index:u32, pagesize:u8 }   // 成员管理路径翻页
```

但两个页面的当前 screen callback 只消费各自的响应 subtype：成员页的
`HandleFactionEquipInfo` 消费 `10/20`，其他帮派页的 `HandleFactionEvents(0x0103FC00)`
只消费 `10/21`。因此服务端不能仅凭 `10/20` 的字段判断当前页面，也不能保留一个容易在
返回菜单后过期的服务端 UI 状态。对歧义请求 `10/20`，服务端在同一 WT 包中依次返回
成员对象 `10/20` 与帮派列表对象 `10/21`；当前页面会忽略不属于自己的对象。明确的
`10/21` 初始帮派列表请求仍只返回 `10/21`，明确的 `10/37` 成员请求仍只返回 `10/20`。

响应为：

```text
1/10/20 {
  result:u8
  cnum:u32             // 当前成员数
  mnum:u32             // 成员上限
  allpgs:u32
  num:u8
  playerinfo:blob
}

repeat num:
  first row: raw-be32 roleId
  later rows: tagged-u32 roleId
  u8  online
  string roleName
  string memberTitle
  u32 memberRank       // 1=帮主, 2=管理, 3=成员
  u32 level
```

客户端解析器首先清 `screen+145` 的网络等待标记；缺少该响应时成员页会一直绘制进度
提示。角色名和位阶名分别写入 44 字节行结构的两个 16 字节槽，服务端发送前按 GBK
字符边界限制为 15 字节，避免覆盖相邻的等级/位阶字段。成员数据来自
`guild_members JOIN account_roles`；确认角色归属后还会读取完整帮派记录，保证
`cnum/mnum` 使用真实成员数和人数上限。在线标记来自当前 mock-service 会话，而不是硬编码。
`playerinfo` 的首行 roleId 采用相同的 raw-BE32 规则。

## 帮派管理菜单状态

ida_evidence:
  binary: `江湖OL.CBE`
  actor_parser: `parse_actorinfo_playerinfo_blob(0x0100FA88)`
  scene_menu: `HandleSceneMenuNavigation(0x0101AED2)`, `0x0101B15E`
  guild_actions: `HandleGuildMenuAction(0x0103D15A)`

`HandleGuildMenuAction` 的各操作分支反复检查当前角色 `actor+104`：非零时进入已入帮
管理/脱离等路径，为零时进入未入帮的列表、申请或提示路径。因此服务端现在发送：

```text
未入帮: actor+100 = roleId, actor+104 = 0,       actor+108 = "无帮派"
已入帮: actor+100 = roleId, actor+104 = guildId, actor+108 = guildName
```

不能通过把显示字符串改成空串来修复菜单；状态必须来自第二个 ID。
主场景菜单 `0x0101B15E` 的零值分支使用 `0x0101B530` 的 GBK 文本
“你未加入任何帮派!”，非零分支才调用 `StartSceneTransition(3,1,1,0)` 打开已入帮管理页。

## 脱离帮派 `10/22`

ida_evidence:
  binary: `江湖OL.CBE`
  confirmation_ui: `DrawGuildInfoPanel(0x0103D2FE)`
  request_sender: `HandleSceneChatInput(0x0103D5F2) -> SendRoleActionEvent(0x0103C830)`
  response_parser: `HandleBattleResultEvent(0x0103DABC)`（旧符号名与实际语义不符）

确认界面显示“是否确定脱离……帮派？”。玩家确认后，客户端发送：

```text
1/10/22 {
  id:u32 = 当前角色 actor+100
}
```

服务端必须同 subtype 返回，且不能返回帮派详情对象：

```text
1/10/22 {
  result:u8                 // 1=成功，0=失败
}
```

`HandleBattleResultEvent` 收到该对象后会清除网络等待状态；成功时显示“成功脱离帮派”并
重置角色状态快照，失败时显示“脱离帮派失败”。旧实现把 `10/22.id=当前角色 ID` 错误地
解析成详情查询并返回 `10/23 {result,faction}`，响应 subtype 与回调契约均不匹配，因而
不能完成脱离流程。

当前服务端在详情处理器之前按当前活动角色 ID 分流，并在 MySQL 事务中执行以下规则：

- 普通成员删除自己的 `guild_members` 关系，并清除自己的待处理入帮申请；
- 帮主且帮派仍有其他成员时返回失败，必须先转让帮主，避免产生无帮主帮派；
- 帮派仅剩帮主本人时允许退出，同时删除帮派，成员和申请由外键级联清理；
- 请求 ID 不是当前活动角色、角色未入帮或数据库事务失败时均返回失败。

service_evidence:
  event: `7`
  handled_source: `builtin-guild-leave`
  response: `10/22 {result}`
  runtime_log: `tmp/mock-service-19090.20260718-115449.stdout.log`
  operational_service: PID `8200`, `127.0.0.1:19090`
  operational_log: `tmp/mock-service-19090.20260718-120118.stdout.log`

真实服务回归用临时两人帮派验证了三条状态转换：有成员时帮主退出得到 `result=0`；
普通成员退出得到 `result=1` 且帮派保留；成员退出后唯一帮主再次退出得到 `result=1`，
帮派与关联成员一并删除。回归夹具已清理，原有帮派、成员和申请数据未改动。

## 申请响应 `10/31`

`HandleFactionCreateResult(0x0103F99E)`（旧符号名与实际语义不符）读取：

```text
result:u8
level:u16
name:string                 // result=1 时读取
```

已确认的结果语义：`1=申请已提交等待批准`、`2=等级不足`、`3=已有帮派`、
`4=成员已满`、`5=申请列表已满`，其余失败使用 `7`。因此服务端把申请写入
`guild_applications(status=0)`，不会把申请伪造成即时入帮。

此前服务端把无字段 `10/30` 错当成申请请求，并用详情页缓存的 `selectedGuildId`
提交申请。实际点击“创建帮派”时因此返回 `10/31 result=7`（运行时表现为 35 字节
响应），客户端创建状态机没有被启动。现在申请 detector 只接受带 `id` 的 `10/31`。

## 创建帮派三阶段

ida_evidence:
  binary: `江湖OL.CBE`
  menu_action: `HandleGuildMenuAction(0x0103D15A)`
  sender: `SendRoleActionEvent(0x0103C830)`
  dispatch: `HandleGuildBusinessDispatch(0x0103C50E)`
  input_confirm: `0x0103CCA8`
  name_result: `0x0103C27E`
  commit_result: `scene_update_status_snapshot_from_packet(0x0103C07E)`

创建过程由客户端状态字段 `screen+144` 驱动，必须保持下列响应 subtype，不可用一个
通用成功包替代：

```text
10/30 {}                 -> 10/27 { result=1 }
10/28 { name }           -> 10/28 { result=1, info:string }
10/11 { name }           -> 10/11 { result=1, id:u32, money:u32, num:u8 }
```

- `10/27 result=1` 把状态切到 1 并打开帮派名称输入框；
- 第一轮确认由状态 1 发送 `10/28`，成功响应的 `info` 是最终确认提示，客户端把状态
  切到 2；重名或非法名称返回 `result=2`；
- 状态 2 再次确认后发送 `10/11`。服务端在同一个 MySQL 事务中插入 `guilds` 和帮主
  对应的 `guild_members(member_rank=1)`，清理该角色旧申请，然后返回真实自增
  `guild_id`；
- 服务端在账号会话中暂存通过校验的名称，最终 `10/11.name` 必须与其一致，避免跳过
  名称校验直接创建；
- 最终失败使用 `10/11 { result=4, msg:string }`，不会留下只有主表、没有帮主成员的
  半成品记录。

## MySQL 数据模型

- `guilds`：帮派主记录、帮主、等级、人数限制、资源、建设和公告。
- `guild_members`：账号/角色到帮派的一对一成员关系；`member_rank=1/2/3` 分别表示
  帮主、管理和成员。
- `guild_applications`：入帮申请及处理状态。

`guilds` 和 `guild_members` 对 `account_roles` 使用 `ON DELETE CASCADE`，用于角色被真正
删除时清理成员关系。角色状态持久化不能再用“删除账号全部角色后重新插入”的刷新方式；
否则普通的角色选择、位置保存或战斗保存都会被数据库解释为角色删除，并连带解散帮派。
当前关系表保存只删除已经不在角色列表中的角色，现有角色使用原位 upsert，装备和背包子表
则单独清空重建。

完整新库使用 `server/mysql/schema.sql`。已有 `jh_online` 使用
`server/mysql/migrate_add_guilds.sql` 手动新增三张表。

## 发布公告 `10/35 + 10/34 -> 10/35`

ida_evidence:
  binary: `江湖OL.CBE`
  menu_dispatch: `DispatchRoleAction(0x01040AD8)`, index 3
  request_sender: `SendRoleActionEvent(0x0103C830)`
  response_dispatch: `DispatchGuildNetEvents(0x0104149E)`
  response_parser: `HandleGuildSloganResult(0x01040FCE)`

发布公告不是一次请求，而是读取和提交两个阶段：

```text
1/10/35 {}                  -> 1/10/35 { result:u8, slogan:string }
1/10/34 { slogan:string }   -> 1/10/34 { result:u8 }
```

菜单确认第 4 项后，`DispatchRoleAction` 先发送空 `10/35` 读取当前公告。成功响应由
`HandleGuildSloganResult(0x01040FCE)` 清 `screen+145` 等待标记、打开公告编辑界面并把
`slogan` 复制到编辑缓冲区。编辑确认时，`HandleChatSceneInput(0x0103FC7E)` 调用
`SendRoleActionEvent` 的 subtype 34 分支发送 `slogan`；其结果由独立的
`HandleChatChannelResult(0x0103FE7E)` 处理。该处理器只接受 subtype 34，只读取
`result`，先清全局等待状态和 `screen+145`，再显示结果并返回帮派管理界面。把发布结果
改写成 `10/35` 会绕过该处理器，造成公告已经写入数据库但进度条无法消失。

服务端只允许 `member_rank=1/2`（帮主/管理）读取和修改公告；提交内容按 GBK 字符边界
验证且最多 60 字节，与 MySQL `guilds.notice VARBINARY(60)` 一致。读取成功在 `10/35`
响应中回显最终文本，读取无权限使用 `result=2`；发布成功只回 `10/34 {result=1}`，发布
无权限使用 `result=3`，畸形内容或存储失败使用失败结果。这样每条路径都进入对应客户端
处理器并清除等待标记。

早期协议级回归虽验证了公告读取、临时公告发布和 MySQL 持久化，但只检查了服务端响应，
没有覆盖客户端发布结果分派，因而错误地接受了 `10/34 -> 10/35`。运行时复测出现“发布后
进度条不消失”后，补充 IDA 取证确认发布必须原样响应 `10/34 {result}`；回归同时检查
读取 `10/35` 与发布 `10/34` 的 subtype、字段集合、WT 对象边界和数据库持久化。
新构建的服务级回归使用 `guest00023/10023` 验证了当前公告读取、18 字节临时公告发布、
畸形空发布 `result=0`、MySQL 复读及原 5 字节公告恢复；发布成功和失败响应均为 23 字节
`10/34 {result}`。回归日志为 `tmp/mock-service-19090.20260718-113536.stdout.log`；回归后
正式服务以 PID `31344` 重启，日志为 `tmp/mock-service-19090.20260718-113907.stdout.log`，
唯一监听 `127.0.0.1:19090`，`CBMS` ping 返回 `CBMR/version=1/bodyLength=0`。

## 设定位阶

ida_evidence:
  binary: `江湖OL.CBE`
  rank_request_sender: `HandleFactionActionInput(0x01042198)`
  replace_request_sender: `SendPKChallengeReq(0x010420E6)`
  rank_page_parser: `HandleGuildRankResponse(0x01041130)`
  action_result_parser: `HandleLoginResponse(0x010428D0)`
  faction_dispatch: `DispatchFactionNetEvents(0x01042CB2)`

成员管理与位阶表共用请求：

```text
1/10/37 { index:u32, pagesize:u8 }
  -> 1/10/20 { result,cnum,mnum,allpgs,num,playerinfo }
  -> 1/10/37 { result,flag,allpgs,num,rank }
```

`rank` 的每行顺序为 `u32 positionId, string positionName, string occupantName,
u32 occupantRoleId`。第一个 `positionId` 和其他 blob 行一样必须使用 raw BE32；后续数值
使用带类型的顺序字段。空位的 `occupantName` 必须是 GBK“无”、`occupantRoleId=0`；客户端
在 `0x010413B6` 用“名称不等于无且 ID 大于 0”判断该位阶是否已经有人。当前持久化模型提供
两个唯一干部位阶：`1=帮主`、`2=管理`，`3=普通成员`。响应 `flag` 返回当前操作者位阶，
用于客户端限制对自己和帮主位阶的操作。

位阶变更的三条真实请求及响应均只需要 `result:u8`：

```text
1/10/38 { aid:u32, id:u32 }
  -> 1/10/38 { result:u8 }       // 把普通成员设到空位

1/10/39 { dname:string, did:u32, drank:u32,
          uname:string, uid:u32, urank:u32 }
  -> 1/10/39 { result:u8 }       // 新旧成员原子交换位阶

1/10/41 { uname:string, uid:u32, urank:u32 }
  -> 1/10/41 { result:u8 }       // 帮主让位
```

服务端只允许当前帮主执行变更，并在每次操作前重新确认目标仍属于同一帮派、请求名称和旧
位阶没有过期。`10/38` 拒绝重复占位；`10/39` 在同一条 SQL 中把原管理降为普通成员并
提升目标；`10/41` 在一个 MySQL 事务中同时交换两人的 `guild_members.member_rank`，并
更新 `guilds.leader_account_id/leader_role_id/leader_role_name`。所有被识别的畸形请求也
返回同 subtype 的失败结果，避免客户端等待标记残留。

协议回归临时加入两个普通成员，依次验证 `10/37` 双对象、`10/38` 空位任命、`10/39`
管理替换和 `10/41` 帮主让位；响应结果均为 1，数据库位阶与帮主外键同步变化。测试结束
后原帮主和成员数据已恢复。日志位于
`tmp/mock-service-19090.20260718-102855.stdout.log`，包含
`mock_guild_rank_page`、`mock_guild_rank_compat` 和三条 `mock_guild_rank_action`。

### 位阶入口的权限门 `10/19`

phase: guild rank dialog gate
status: validated

request:
  wt_kind: 10
  wt_subtype: 19
  objects: `1/10/19`, empty payload
  key_fields: none
  sample_len: 9
  packet_log: `tmp/mock-service-19090.20260718-110755.stdout.log`

response:
  wt_kind: 10
  wt_subtype: 19
  objects: `1/10/19`
  fields: `result:u8`

ida_evidence:
  binary: `江湖OL.CBE`
  function: `DispatchRoleAction(0x01040AD8)`, call at `0x01040B36`
  dispatch_case: `DispatchGuildNetEvents(0x0104149E)`, subtype 19
  parser_reads: `HandleDialogResult(0x01041094)` reads only `result`; entry clears
    the global network wait state and `screen+145`
  failure_branch: `result=2` displays “权限不足”; other failures display “操作失败”

运行时中，设定位阶入口完成 `10/37` 后紧接着发送 9 字节空对象 `10/19`。旧服务未识别
该包，记录 `unhandled wt=10/19 len=9` 后触发服务端断言，客户端因此一直显示进度条。
现在 `builtin-guild-dialog-gate` 对当前帮主返回 `result=1`，由客户端调用自身的后续回调；
非帮主或已经离帮返回 `result=2`。真正的位阶写入仍由 `10/38/39/41` 独立复核，权限门
本身不改客户端或数据库状态。

服务级回归对同一份 9 字节请求验证：帮主 `guest00023/10023` 收到 `result=1`，未入帮
角色 `guest00024/10024` 收到 `result=2`；两份响应均为 23 字节单对象 `10/19`，服务继续
监听且没有未处理断言。日志位于
`tmp/mock-service-19090.20260718-111956.stdout.log`，handled source 为
`builtin-guild-dialog-gate`。

## 逐出帮派 `10/40`

ida_evidence:
  binary: `江湖OL.CBE`
  request_sender: `ProcessBattleSceneInput(0x01042D18)`, call at `0x01042D92`
  response_dispatcher: `HandleFactionBattleResult(0x01042F16)`
  response_parser: `HandleBattleMenuResult(0x01042E3E)`

成员页选择“逐出”并确认后发送：

```text
1/10/40 { id:u32, rid:u32 }
  -> 1/10/40 { result:u8 }
```

`id` 是目标角色 ID，`rid` 是成员页当前行记录的旧位阶。客户端结果码为：`1` 操作成功
并重新请求当前成员页，`2` 权限不足，`3` 目标已经离帮，`4` 目标是干部、必须先降为
帮众，其余值显示“操作失败”。服务端据此只允许当前帮主逐出同帮的普通成员
（`member_rank=3`），同时拒绝逐出帮主/管理、过期位阶、跨帮目标和无效角色。

成功路径在 MySQL 事务中按 `guild_id + account_id + role_id + member_rank=3` 精确删除
`guild_members` 行，并在提交前复查该成员关系已消失。畸形 `10/40` 仍返回同 subtype 的
失败结果，以保证客户端清除等待状态；操作日志为 `mock_guild_kick`。

服务级协议回归使用 `guest00023/10023` 作为帮主、临时加入
`guest00024/10024`：普通成员逐出返回 `1` 且成员行消失；逐出管理返回 `4` 且成员保留；
普通成员越权返回 `2`；过期位阶与缺少 `rid` 的畸形包均返回 `5`；目标删除后重试返回
`3`。测试结束后临时成员关系已删除，数据库恢复为原状态。六条分支的服务日志位于
`tmp/mock-service-19090.20260718-110008.stdout.log`。

## 验证

使用真实 37 字节 `10/20`、`10/21` 与 `10/37 { index=1, pagesize=10 }` 请求验证：

- 空库返回 `10/21`, `resp=66`, `rows=0`, `allpgs=1`；
- 当前单帮派数据返回 `10/21`, `resp=94`, `rows=1`，`faction` 数据以 raw-BE32
  `00000004` 开头；
- 已入帮管理页的 `10/23 {id=4}` 返回 `10/23`, `resp=104`，完整 `faction`
  长 69 字节，前缀为 `00000004 0007 BADAC1FAB0EF 00`；
- `10/37 {index=1,pagesize=10}` 返回双对象 `10/20 + 10/37`；三成员回归夹具下
  `resp=306`，位阶表包含两个位置且 `flag=1`；歧义的
  `10/20 {index=1,pagesize=10}` 返回双对象 `10/20 + 10/21`, `resp=226`；明确的
  `10/21` 仍为单对象 `10/21`, `resp=110`。三个 WT 包的对象长度都精确结束在包尾；
  成员数据长 33 字节，
  创建者行以 raw-BE32 roleId `00002727` 开头，位阶为“帮主”；
- 带目标帮派 ID 的 `10/31` 返回 `10/31`，并产生一条待处理申请；
- 解码角色选择响应的 `actorinfo` 后，未入帮角色得到
  `roleId=10024, guildId=0, guildName=无帮派`；临时加入测试帮派后得到
  `roleId=10024, guildId=<真实主键>, guildName=测试帮`；
- 早期把已入帮管理页的 `10/22 {id=当前角色 ID}` 解析为本帮详情并返回 `10/23`
  是错误路径；IDA 发送点和结果回调共同确认该形态实际是脱离帮派请求，现已在详情处理器
  之前分流并返回同 subtype 的 `10/22 {result}`；
- 测试帮派、成员与申请均在验证结束后清理。

成员页兼容响应的服务验证日志为
`tmp/mock-service-19090.20260718-082944.stdout.log`，新构建已由该服务进程监听
`127.0.0.1:19090`。

## 招收帮众中的角色资料 `10/2`

ida_evidence:
  binary: `江湖OL.CBE`
  request_sender: `SendShopType2Event(0x0101A5AA)`
  response_parser: `HandleFactionDegreeResponse(0x010211A8)`
  response_wrappers: `HandleFactionAndEquipInfo(0x01021B1A)`, `HandleFactionEquipInfo(0x010420B8)`

“招收帮众”候选页点击“查看角色信息”发送：

```text
1/10/2 { id:u32, type:u8=2 }
```

发送函数同时把社交操作等待标记 `manager+2412` 置为 1；客户端只有在分发到响应
`10/2` 后才会先清除此标记。旧服务端只在目标仍属于当前场景的 nearby seed 时构造
`10/2`，而候选页当前还可能返回本角色或其他场景的在线角色。解析失败后请求落入通用
`type=2` fallback，被错误改写成响应 `10/20 {result,pcimg}`，当前页面不消费该对象，
所以进度条永久保留。

角色资料目标现在按 `nearby -> friend -> online persistent role -> active persisted role`
的窄范围解析。即使角色已离线或 ID 失效，也返回 `10/2 {result=0}`，让同一个解析器
清等待标记并显示错误提示；不再允许该请求落入通用 `game-type` 响应。

服务级回归使用与客户端一致的 30 字节请求验证：已存在的角色 `10023` 返回单对象
`10/2`, `resp=126`，包含 `type/fdegree/num/playerinfo` 且 WT 声明长度精确等于包长；
不存在的角色返回单对象 `10/2 {result=0}`, `resp=23`，同样精确结束在包尾。成功路径日志
为 `mock_player_info actor=10023 scope=active-role ... source=builtin-nearby-player-info`，未再
出现该请求被 `builtin-game-type` 接管。验证产生的临时账号状态已经删除。

## 招收帮众申请列表与审批 `10/32 + 10/33`

phase: guild-recruitment-application-review
status: implemented-and-service-validated

ida_evidence:
  binary: `江湖OL.CBE`
  page_sender: `SendRankingPageReq(0x01040276) -> SendRoleActionEvent(0x0103C830)`
  action_sender: `SendRoleBattleAction(0x010402BC) -> SendRoleActionEvent(0x0103C830)`
  page_parser: `HandleRoleListResponse(0x0103FF2C)`
  action_parser: `HandleRankingResponse(0x0104094A)`
  dispatcher: `HandleRankingEvents(0x01040AA8)`
  menu_input: `HandleBagMenuInput(0x01040318)`

客户端进入“招收帮众”时发送固定 48 字节请求：

```text
1/10/32 {
  id:u32 = 0
  index:u32        // 一基首行索引
  pagesize:u8
}
```

`HandleRoleListResponse(0x0103FF2C)` 在 `result=1` 时依次读取：

```text
roles:u32          // 本帮当前人数
maxroles:u32       // 本帮人数上限
allpgs:u32
num:u32
roleinfo:blob
repeat num:
  roleId:u32
  roleName:string
  level:u32
```

`result=3` 表示权限不足，`result=4` 表示当前没有申请人。旧服务端把所有
`10/32` 错误分流到登录角色列表 builder，后者从当前账号的活动角色构造
`roleinfo`；因此帮主使用另一个账号申请后，列表仍稳定显示帮主本人。数据库中的原始
申请并没有串号：运行取证记录为
`guild_id=4, applicant_account_id=guest00024, applicant_role_id=10024`，申请人名称也
与 `account_roles` 一致。修复后的列表只读取当前帮派
`guild_applications.status=0`，角色 ID、名称和等级全部来自申请快照，不再读取操作人的
活动角色。

选择“批准”或“拒绝”时，客户端发送固定 34 字节请求：

```text
1/10/33 {
  type:u8          // 1=批准，2=拒绝
  roleid:u32       // 申请人角色 ID
}
```

响应保持相同 subtype：`1/10/33 {result:u8,type:u8}`。`HandleRankingResponse`
对 `result=1` 刷新当前申请页；批准时还增加客户端当前人数。其他结果的客户端语义为：
`2=权限不足`、`3=人数已满`、`4=玩家已有帮派`、`5=申请已被处理`。旧通用
`type` fallback 会把批准和拒绝响应分别改写成其他 subtype，页面既收不到正确回调，
服务端也完全没有修改成员关系。

审批现在先复核操作人仍是本帮帮主/管理、申请仍待处理、人数上限以及申请人是否已加入
其他帮派。批准在同一 MySQL 事务中插入 `guild_members(member_rank=3)` 并把申请状态
改为 `1`；拒绝只把状态改为 `2`。过期申请不会误操作其他角色。

### 在线申请人的结果通知 `10/36`

批准申请最初只修改了 MySQL。在线申请人的角色节点仍保留登录时解析出的
`actor+104=0`，所以服务端成员查询已经认为角色入帮，但客户端主菜单仍按旧值进入
“未入帮”分支；重新登录重新解析 `actorinfo` 后才恢复。客户端本身提供了无需重登的
状态更新协议：

ida_evidence:
  binary: `江湖OL.CBE`
  dispatch: `HandleGuildBusinessDispatch(0x0103C50E)`, subtype `36`
  parser: `scene_update_status_summary_from_packet(0x0103BCBA)`
  reset_parser: `scene_reset_status_snapshot_nodes(0x0103BC3C)`
  copy_helper: `scene_copy_status_summary_fields(0x01044FAC)`

批准通知为：

```text
1/10/36 {
  type:u8 = 1
  info:string                 // 服务端中文成功提示
  id:u32 = guildId
  name:string = guildName
  status:u32 = 3              // 普通帮众
}
```

解析器先显示 `info`，再把 `id/name/status` 分别写入当前角色节点的
`+104/+108/+312`，并通过 `scene_copy_status_summary_fields` 同步状态快照和状态显示
节点。因此后续菜单和人物信息立即使用新的帮派 ID，不再依赖重新登录。拒绝通知为：

```text
1/10/36 {
  type:u8 = 2
  info:string                 // 申请被拒绝提示
}
```

`type=2` 只报告申请结果，不携带 `id/name/status`，不会覆盖角色可能已经从其他途径获得的
帮派身份。远程 mock 服务没有服务端主动连接客户端的通道，因此审批事务提交后把通知写入
目标在线会话队列，由目标已有的 event-7 场景同步轮询下发；目标离线时不保留易过期的内存
通知，数据库仍是权威状态，下次登录由 `actorinfo` 恢复。

服务级在线回归使用三个独立会话：`guest00022/10022` 审批，
`guest00021/10021` 接收批准，`guest00020/10020` 接收拒绝。批准目标从自己的轮询响应中
得到 `10/36 type=1,id=<临时帮派>,name=codexNtf18,status=3`；拒绝目标得到
`10/36 type=2` 且没有帮派字段。队列和投递目标日志分别为
`guild_application_notice_queue`、`guild_application_notice_deliver`，完整记录位于
`tmp/mock-service-19090.20260718-130656.stdout.log`。测试夹具已级联清理。

服务级回归使用 `guest00022/10022` 作为临时帮主、`guest00020/10020` 和
`guest00021/10021` 作为两个真实申请人：`10/32` 返回角色 ID
`[10020,10021]`，且不含操作人 `10022`；批准 `10021` 后成员位阶为 3、申请状态为
1；拒绝 `10020` 后没有成员行、申请状态为 2；再次分页返回 `result=4`。测试完成后
临时帮派、成员及申请已级联删除，既有
`guest00024/10024 -> guild_id=4, status=0` 记录保持不变。对应服务日志为
`tmp/mock-service-19090.20260718-122632.stdout.log`。

本次创建修复额外完成了静态协议复核和 `make -j2` 构建验证。完整交互需要在客户端
依次输入名称并确认两次；服务日志应依次出现 `mock_guild_create_start`、
`mock_guild_create_name`、`mock_guild_create_commit`。

unknowns:
  - name: 入帮邀请目标端确认
    current_value: 未实现
    why_kept: `10/13` 发送端已确认，但目标端 `10/14` 及确认后的成员变更尚未完成跨客户端运行验证。
