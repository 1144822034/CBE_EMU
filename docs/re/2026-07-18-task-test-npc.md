# 测试任务 NPC 与接取链路

## 目标

在测试角色当前使用的 `00蓬莱仙岛_02.sce`（蓬莱-铸剑谷）中加入服务端 NPC“任务使者”，通过客户端原生任务界面接取测试任务，并按账号、角色持久化任务状态。

## 客户端证据

- `ParseNPCDialogData(0x010380E8)`：NPC 对话选项为 `display-type/name/action/value/description`；解析后 `action` 位于选项记录 `+44`，`value` 位于 `+48`。
- `task_hall_activate_selected_entry(0x010492B0)`：确认当前选项时读取记录 `+44`；只有 `action=4` 才会调用 `task_hall_request_task_detail` 并发送 `6/10`。
- `task_hall_request_task_detail(0x010491FA)`：发送 `1/6/10 {taskid,state}`。
- `ReqTaskInfo(0x01038D2C) -> SendTaskHallReq(0x01038CB2)`：通过字符串访问器读取响应 `info` 并直接复制为任务详情正文；`info` 不是 tagged raw stream。
- `SendTaskInfoReq(0x01047A7C)`：确认接取时发送 `1/6/11 {taskinfo}`。
- `net_handle_task_response_dispatch(0x0104726C)`：`6/11` 成功响应读取 `result=0` 和 `taskinfo`。
- `ParseItemDataFields(0x01046D24)`：解析活动任务记录；`6/1` 用 `tasknum + taskinfo` 恢复任务列表。
- `RefreshRoleSelectDetail(0x010481EA)`：点击活动任务后先以空字符串清空详情文本控件，再调用 `SendTaskProgressEvent(0x01047E0C)`。
- `SendTaskProgressEvent(0x01047E0C)`：发送 `1/6/12 {id}`；`task_handle_destinfo_response(0x01047F0A)` 的 `6/12` 分支读取 WT string 字段 `text`，填充任务目的地/目标说明。
- `DeserializeRoleInfo(0x01046E00)`：把 `6/14 taskinfo` 原始流解析为 76 字节的可接任务记录，其中第二个字符串位于记录 `+37`，是用于匹配场景 NPC 名称的字段。
- `scene_refresh_interact_prompt_types(0x01017C6C)`：按任务记录和 NPC 名称计算 `node+326`。候选任务对应类型 `2`（正常感叹号）；活动任务 `state=1` 对应类型 `3`（灰色问号）；活动任务 `state=2` 对应类型 `1`（正常问号）。
- `task_hall_accept_prompt_dispatch(0x010495E4)`：接取成功时从本地候选任务表删除对应的 76 字节记录，再重新计算头顶图标。
- `CommitTaskProgress(0x01047CFC)`：完成态详情确认后发送 `1/6/4 {taskid}`。
- `net_handle_task_response_dispatch(0x0104726C)` 的 case 16 调用 `HandleTaskCompleteResult(0x01038E6E)`；因此 `6/4` 是上行请求，完成结果必须下发为 `6/16`。成功后客户端从活动任务表删除任务，并读取经验、活力字段。

IDA 实例按 `binary_name=江湖OL.CBE` 选择，未固定 instance id。

## 服务端契约

- 场景 NPC：actor `20022`，名称“任务使者”，模型 `n_man1.actor`，坐标 `(300,125)`。
- 测试任务：task id `900001`，名称“测试任务”。
- `6/14`：只在任务 NPC 所在场景且角色尚无该任务记录时下发一条候选任务；客户端据此显示正常感叹号。
- NPC 对话：未接取时下发一个 `action=4` 选项；进行中再次交互时把状态推进到 `2`，并在同包追加 `6/6` 状态通知，使灰色问号切换为正常问号；完成后再次交互提供“提交测试任务”。
- `6/10`：以 WT string 字段下发任务详情 `info`。
- `6/11`：写入 `account_role_tasks`，成功后下发完整活动任务记录。
- `6/12`：活动任务详情页按任务 ID 请求目标说明，服务端以非空 WT string 字段 `text` 返回。
- `6/1`：登录或场景任务子集刷新时从 MySQL 恢复状态 `1/2` 的活动任务；状态 `3` 不再回放到客户端活动任务表。
- `6/6`：保存并回送任务状态。
- `6/4 -> 6/16`：仅允许状态 `2` 的任务提交，成功后保存状态 `3`；响应保留角色当前经验、活力字段，避免客户端解析缺省零值覆盖角色状态。

场景资源组合包调整为先处理 `2/10 otherinfo` 创建 NPC 节点，再处理 `6/1` 和 `6/14`。这是必要的，因为客户端只有任务响应分支会调用 `scene_refresh_interact_prompt_types`；旧顺序会在任务刷新时尚未创建 NPC 节点，导致首次登录不显示头顶图标。

测试任务的首个要求使用 `type=1/id=65535/current=0/required=1`，用于保持任务处于进行中，避免普通背包事件误完成。后续任务进度、交付和奖励逻辑应在该持久化记录上继续扩展。

## 2026-07-18 回归

- `make -j2` 通过。
- 独立 service clientId 完成账号登录、`26/1` NPC 对话、`6/10` 详情和 `6/11` 接取回放。
- 响应长度依次为 `123 / 78 / 133`；NPC 对话包含 task id `900001`。
- 接取后任务子集响应为 6 个对象，`6/1` 长度 `129`，MySQL 行为 `state=1/progress1=0/progress2=0`。
- 回归产生的角色任务行已删除，测试角色状态未被预置为已接取。

## 2026-07-18 图标与完整状态回归

- `make -j2` 通过。
- 使用独立 service clientId 按顺序验证：首次任务子集、NPC 对话、`6/11` 接取、任务子集恢复、NPC 再交互完成、`6/4` 提交、提交后任务子集。
- 首次任务子集响应长度 `276`，`6/14` 含 task id `900001`；接受后任务子集长度 `283`，`6/1` 为 `state=1` 且 `6/14 tasknum=0`。
- 完成交互响应长度 `125`，同包包含 `26/1 + 6/6 state=2`；提交响应长度 `121`，提交后任务子集长度 `184`，不再包含 task id。
- 运行日志：`tmp/mock-service-19090.20260718-160904.stdout.log`；stderr 为 0。
- 回归结束后断开测试 service clientId，并删除其测试任务行，未预置用户角色任务状态。

## 2026-07-18 接取确认修正

真实客户端复测只反复出现 `26/1 -> builtin-npc-dialog`，没有发出 `6/10`。原因是旧实现把 `4` 写入了 `display-type`，却把真正由确认分派读取的 `action` 写成 `0`；确认时因此执行 `ResetDownloadState`，只关闭页面而不会请求任务详情。服务端现保留显示类型，并把选项 `action` 改为 `4`。该字段解释由 `ParseNPCDialogData(0x010380E8)` 的记录写入位置和 `task_hall_activate_selected_entry(0x010492B0)` 的 `switch(option+44)` 共同确认。

同轮系统性核对还修正了后续两段契约：`6/10 info` 改为 `SendTaskHallReq` 实际读取的 WT string；最终提交仍由客户端上行 `6/4`，但服务端改为下发任务分派 case 16 所需的 `6/16` 完成结果。

- `make -j2` 通过。
- 独立 service clientId 验证 NPC 选项序列中任务名后为 `action=4 + taskid=900001`，响应长度 `123`。
- `6/10` 详情响应长度 `68`，对象为 `6/10` 且包含纯中文正文；`6/11` 接取响应长度 `133`。
- 完成交互返回 `26/1 + 6/6`，长度 `125`；上行 `6/4` 得到下行 `6/16`，长度 `121`。
- MySQL 状态依次到达 `1/2/3`；回归后删除测试任务行。运行日志为 `tmp/mock-service-19090.20260718-162553.stdout.log`，stderr 为 0。

## 2026-07-18 真实接取请求修正

真实客户端在确认“是否接取任务”后发送长度 `31` 的复合 WT 包：

- 首对象 `1/6/11`，payload 长度 `17`；`taskinfo` 的 entry value 直接是六字节 tagged-u32 任务 ID，没有 `put_object_blob` 的第二层长度封装。
- 尾对象 `1/25/5`，payload 长度 `0`。

旧处理器只接受恰好一个对象，并只用 nested-blob 解析 `taskinfo`，因此该包记录为 `unhandled wt=6/11 len=31` 后触发服务端断言，客户端只剩无法消失的进度条。IDA 中 `SendTaskInfoReq(0x01047A7C)` 明确通过 `stream_reader_init_from_blob(..., "taskinfo")` 后直接写入任务 ID，和真实 payload 长度相符。

处理器现允许且只允许 `6/11 + 空 25/5` 这一种尾对象组合；任务 ID 优先按直接 tagged-u32 entry 读取，同时保留旧 nested-blob 兼容。成功响应包含 `6/11` 任务记录及 `25/5 result=4`，完整应答客户端同一复合请求。

- `make -j2` 通过。
- 回归请求严格复现真实布局和总长度：`6/11 payload=17 + 25/5 payload=0`，WT 总长 `31`。
- 响应长度 `151`，同时包含 `6/11`、task id `900001` 和 `25/5`；MySQL 实际写入 `task_state=1`。
- 回归后断开测试 service clientId 并删除测试任务行。运行日志为 `tmp/mock-service-19090.20260718-163830.stdout.log`，stderr 为 0。

## 2026-07-18 活动任务详情闪退修正

接取后点击任务列表条目时，客户端在 `LayoutTextWithWordWrap(0x01006C46)` 的
`0x01006DE6` 对空指针写入。栈中保存的调用返回地址为 `0x01048209`；对应
`task_detail_open_selected(0x010481EA)` 先把 `aDestinfo+8`（空 C 字符串）传入
详情文本控件，再调用 `task_request_detail_text(0x01047E0C)`。客户端会调用
`DF_Malloc_IN(slot, 0)` 为零行文本准备索引数组，并立即写入首个零偏移；模拟器
此前把零字节申请翻译成 NULL，和客户端依赖的目标 ABI 不一致。

模拟器现只在 DreamFactory 槽位分配接口中把零字节规范为一个 2 字节可写块，
与客户端随后写入的 16 位行偏移哨兵一致，并保留普通 `vm_malloc(0)` 的原有语义。
同时服务端实现点击详情后紧接着产生的
`6/12 {id}` 请求，返回非空 WT string `text` 作为任务目标说明。MySQL 活动任务
查询也对陈旧持久连接的 `socket-send-failed` 做一次安全 SELECT 重试，避免重登时
短暂下发 `tasknum=0` 并重复显示可接任务。

- `make -j2` 通过。
- 以已接取任务的 `guest00023/10023` 回归 `6/12 {id=900001}`，得到单对象
  `6/12 {text}`，响应长度 `60`，中文正文长度 `40`。
- 正式服务日志为 `tmp/mock-service-19090.20260718-170412.stdout.log`，stderr 为
  `0`，服务保持监听 `127.0.0.1:19090`。

## 2026-07-18 任务提交复合包修正

真实客户端在完成态任务详情中确认提交后发送的不是单独 `6/4`，而是总长 29
字节的复合请求，顺序固定为：

- `1/25/5`，空 payload；
- `1/6/4`，payload 长度 15，字段 `taskid=900001`。

旧处理器只从首对象匹配任务 kind，因此把该包记录为
`unhandled wt=25/5 len=29 first=1/25/5:0,1/6/4:15`，没有任何响应，客户端
的等待进度条也就不会关闭。`CommitTaskProgress(0x01047CFC)` 在
`0x01047D56` 创建 `6/4 {taskid}`；任务响应分派 case 16 在 `0x010479D2`
调用 `HandleTaskCompleteResult(0x01038E6E)`，读取 `result/taskid` 并从活动任务
列表删除任务。

任务处理器现只额外接受这一种精确的前置组合，并保持请求语义顺序返回两个对象：

- `1/25/5 {result=4}`：结束等待提示；
- `1/6/16 {result=0,taskid,...}`：提交完成结果。

其他任务请求仍必须以 `1/6/*` 开头；`6/11 + 空 25/5` 的既有接取组合保持不变，
避免前置等待对象规则误吞其他复合包。

- `make -j2` 通过。
- 回归严格发送真实 29 字节请求，收到长度 `139`、对象顺序
  `25/5 + 6/16` 的响应；服务日志显示
  `request_info_prefix=1 response_objects=2 result=0`。
- 回归在事务式清理段恢复原任务状态，未替用户预先提交任务。
- 正式服务日志为 `tmp/mock-service-19090.20260718-172058.stdout.log`，stderr 为
  `0`，服务保持监听 `127.0.0.1:19090`。

## 2026-07-18 删除任务修正

真实客户端在活动任务详情中确认“删除任务”后发送单对象请求
`1/6/7 {taskid}`，总长 24、payload 长度 15。旧任务处理器未把 subtype 7
列入任务请求白名单，运行时因此记录
`unhandled wt=6/7 len=24 first=1/6/7:15`；服务端进程断言退出，客户端等待提示
无法结束。

IDA 中 `task_request_abandon_by_taskid(0x01047DAC)` 明确创建
`1/6/7 {taskid}`。任务响应分派 case 7（`0x0104778C`）读取字段 `result`：
`result=0` 时按此前保存的 task id 清除活动任务记录，并显示客户端内置文本
“删除成功”；非零时显示“删除任务失败”。该分支不读取任务详情或奖励字段。

服务端现对测试任务执行以下窄契约：

- 仅接受活动状态 `1/2` 的 `taskid=900001`；
- 从 `account_role_tasks` 删除对应账号、角色和任务行，使任务随后可重新接取；
- 返回单对象 `1/6/7 {result=0}`，由客户端正常关闭等待提示并删除本地任务。

- `make -j2` 通过。
- 回归发送真实长度 24 的 `6/7` 请求，收到长度 `23` 的单对象成功响应；
  MySQL 行已在响应前删除，回归结束后恢复测试前状态。
- 正式服务日志为 `tmp/mock-service-19090.20260718-173218.stdout.log`，stderr 为
  `0`，服务保持监听 `127.0.0.1:19090`。
