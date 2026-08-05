# 铜雀台大侠郭靖任务接取失败排查（2026-08-04）

Date: 2026-08-04

Status: validated configuration diagnosis

## 1. 当前卡点

- 可见现象：动态 NPC「大侠郭靖」已绑定任务 1「初来乍到」，对话能进入任务详情，但确认
  接取后任务没有进入任务列表。
- 触发方式：角色 `guest00001/10001` 进入 `c00蓬莱仙岛_01.sce`，与 Actor `30001`
  对话，选择任务并确认接取。
- 本轮最小目标：确定 `6/11` 接取响应为何返回失败，并只修复最早失败的服务端契约或持久化
  操作。

## 2. 运行时证据

`bin/server_out.txt` 的同次会话记录：

```text
mock_scene_npc_catalog ... scene=c00蓬莱仙岛_01 ... actors=2 selected=2
mock_task_candidates role=10001 scene=c00蓬莱仙岛_01 tasknum=1 ...
mock_npc_dialog actor=30001 ... catalog_match=1 ... task_offer=1 ... task_option_action=4 ...
mock_task action=detail task=1 ... request_subtype=10 response_subtype=10 result=0 ...
mock_task action=accept task=1 role=10001 request_subtype=11 response_subtype=11 result=1 ... taskinfo_len=0 ...
```

这证明动态 NPC、场景候选、NPC 对话、客户端的 `6/10` 详情请求和 `6/11` 确认请求均已走到
正确处理器。首次偏离是 `6/11` 被构造成失败响应；客户端未能获得活动任务记录。

只读数据库查询显示：

```text
account_roles: guest00001 / role 10001 / level 4 / backpack_capacity 20 / backpack rows 20
account_role_tasks: role 10001 当前无记录
server_dynamic_npc_tasks: c00蓬莱仙岛_01 / actor 30001 / task 1 / repeatable 0
```

直接解析原版 `web/fs/JHOnlineData/task.dsh` 的任务 1：等级要求 1、前置任务 0、给予物品
0、给予数量 0。该原版定义不是本次运行时的最终权威数据：服务端会再应用 `server_tasks`
覆盖。

## 3. IDA 目标

| binary | function/address | reason | findings |
| --- | --- | --- |
| `江湖OL.CBE` | `SendTaskInfoReq` `0x01047A7C` | 确认接取上行 | 参数为真时将请求 subtype 改为 `11`，并向 `taskinfo` 写入任务 ID。 |
| `江湖OL.CBE` | `net_handle_task_response_dispatch` `0x0104726C` case 11 | 确认下行接取契约 | `6/11` 成功分支读取 `result=0` 和 `taskinfo`；没有活动任务流时客户端不会建立任务条目。 |
| `江湖OL.CBE` | `ParseNPCDialogData` `0x010380E8` | 确认 NPC 选项 | `action=4,value=task_id` 是原生任务详情/接取路径。 |

IDA 实例按 `binary_name=江湖OL.CBE` 动态选择，未固定实例 ID。

## 4. 调用链 / 业务流程

1. 场景 `25/5 + 6/14` 返回任务 1 候选，说明同一角色的
   `vm_net_mock_task_definition_available()` 判定为真。
2. `26/1` NPC 对话匹配 Actor 30001，记录任务 offer context 并下发 `action=4`。
3. 客户端发送 `1/6/10` 读取详情，随后发送 `1/6/11 {taskinfo=<tagged task id>}`（允许带空
   `25/5` 尾对象的真实复合包）。
4. `vm_net_mock_build_task_response()` 对 `6/11` 依次加载任务状态、重新判定可接、调用
   `vm_net_mock_task_accept()` 插入 `account_role_tasks`、回读状态，并仅在成功时构造
   `taskinfo`。
5. 当前日志显示第 4 步的合并布尔判定结果为假，但没有记录是状态读取、资格校验、写入还是
   回读中的哪一项先失败。

## 5. 请求 / 响应契约

### Request

- WT：`1/6/11`，可带一个空 `1/25/5` 尾对象。
- 字段：`taskinfo`，直接 tagged-u32 的任务 ID（本例为 1）。

### Response

- WT：`1/6/11`，并在复合请求时保留匹配的 `25/5` 完成对象。
- 成功字段：`result=0`、完整 `taskinfo` 活动任务流。
- 当前失败字段：`result=1`、无 `taskinfo`。

## 6. 已排除项

- 不是场景键、动态 NPC actor、NPC 对话 action 或任务前置条件错误：候选及 `action=4` 已由
  运行日志证实。
- 不是任务 1 的接取物品/背包容量限制：原始任务定义的给予物品与数量均为零。
- 不是角色等级不足：角色等级 4，高于任务要求 1。

## 7. 根因

本次新日志为：

```text
mock_task_accept_rejected task=1 role=10001 offer_context=1 repeatable=0
state_list=1 state_rows=0 definition_available=1 backpack_can_receive=0
write_ok=0 post_write_load=0 persisted=0 state=0 mysql_error=no MySQL error detail
```

随后只读查询 `server_tasks` 的运行时覆盖记录得到：

```text
task_id=1, enabled=1, level=1, prerequisite_task_id=0,
given_item_id=807, given_item_count=3
```

角色 `10001` 的背包容量和已用槽位都是 20，且没有可与物品 807 合并的现有槽位。因此，
接取任务时 `vm_net_mock_task_backpack_can_receive()` 正确拒绝了本应发放的 3 个物品 807；
`vm_net_mock_task_accept()` 没有执行，客户端收到合法的失败 `6/11 {result=1}`。

根因是后台的任务 1 覆盖定义增加了接取物品，而角色背包已满；不是郭靖 NPC、任务前置、
客户端对话或 MySQL 写入问题。原版 `task.dsh` 中的“无给予物品”被数据库覆盖，不可作为
当前运行时定义。

## 8. 正确处理方式

二选一：

1. 若任务 1 本来就应发放物品 807：先在背包中腾出至少一个可用槽位，再接取任务；物品
   807 的数量 3 将在同一槽堆叠，不需要腾出三个槽位。
2. 若任务 1 不应发放物品：在后台“任务管理”编辑任务 ID 1，将“给予物品 ID”和“给予
   数量”同时设为 0，或恢复 task.dsh 默认任务定义。

不应绕过满背包校验或把失败 `6/11` 改为成功；那会使客户端活动任务与应发放物品的服务端
状态失去原子性。

## 9. 验证清单

- [x] 请求被既有任务 detector 命中。
- [x] 已确认 `6/11` 成功响应的客户端 parser 条件。
- [x] 未写入或修改用户任务状态进行取证。
- [x] 失败诊断定位到背包接收判定，并已在确认后删除，未保留常驻取证日志。
- [x] 未修改任务接取协议或用户账号数据。
- [ ] 用户按上述任一正确配置路径复测接取、任务列表、场景问号与后续白展堂任务链。

## 10. 构建

临时取证版本曾通过 `make -j2`。确认根因后，取证代码已移除；最终源码仍需在本轮结束前
重新执行构建验证。

## 11. 接取失败反馈与后台表单默认值（2026-08-04）

### 触发条件与首次偏离

当任务运行时覆盖带有非零 `given_item_id/given_item_count`，且该物品不能与既有槽位合并、
背包也没有空槽时，`vm_net_mock_task_backpack_can_receive()` 正确拒绝接取。此前响应仍是合法的
`1/6/11 {result=1}`，并保留客户端同包请求所需的 `1/25/5 {result=4}` 完成对象；但没有任何
可见的失败原因。这是用户可观察流程第一次偏离：拒绝是正确的，缺少原因提示则不是。

这次不能把所有 `6/11 result=1` 都显示为背包不足，因为同一失败结果还可能来自等级、前置任务、
已接/已完成状态或持久化失败。提示条件必须严格限于：任务定义可用、任务资格判定已经通过、
任务确实会发放起始物品，且接收物品的背包容量检查返回假。

### 客户端契约

本轮按 `binary_name=江湖OL.CBE` 动态选择 IDA 实例，重新核对
`net_handle_info_banner_state` (`0x01010C7E`)：

- `25/11` 仅在 `result=8` 时读取 `info`，将其复制到中央信息提示缓冲区并显示；
- 原任务请求中的 `25/5 {result=4}` 仍是已存在的完成对象，必须保持在 `6/11` 之后；
- 因此满包失败的安全复合响应顺序是 `6/11(result=1) + 25/5(result=4) + 25/11(result=8,info)`。

`25/11.info` 是 GBK 字段，不能发送 UTF-8 源码文本。项目已有
`vm_net_mock_append_info_banner_text11_object()` 使用同一客户端契约；本次仅复用它并传入显式
GBK 字节“背包空间不足，无法接取任务。”。

### 后台表单边界

任务页的三个“NPC 对话覆盖”字段在服务端保存为空时会使用安全默认文案。管理页中单独的 ASCII
`-` 没有对话含义，只是未覆盖的占位值；本次只在**渲染表单**时将这个精确占位值显示为空字符串。
不会改动任务目录、运行时默认对话或数据库记录；管理员主动保存后才会把空覆盖写入数据库。

### 计划修改点

1. `src/server/mock_server_scene_sync.c`：在 `6/11` 接取的既有背包判定处记录窄范围失败原因；
   仅该原因成立时在原 `25/5` 完成对象之后追加 `25/11` 提示。
2. `src/web_admin_server.c`：GBK 转 UTF-8 后，在页面展示层将三个对话字段的单独 `-` 归一为空。

这两处都不改变背包校验、任务写入原子性或客户端任务 parser 的成功条件。

### 实现与验证

- `mock_server_scene_sync.c` 已将原先短路的资格/背包合并判定拆成等价的两步，以便仅在
  “资格通过但起始物品无法接收”的情况下标记失败原因；任务状态插入、起始物品发放和失败
  `result=1` 的既有逻辑未改。
- 原请求带空 `25/5` 尾对象时，响应对象顺序为
  `6/11(result=1) + 25/5(result=4) + 25/11(result=8,info)`；不带尾对象时只省略原本就不存在的
  `25/5`。新增 `mock_task_accept_backpack_full` 日志仅记录这个已确认分支。
- 管理页只将转码后的精确字符串 `"-"` 清为空；非空自定义对话保持不变。
- `git diff --check` 除仓库既有的 CRLF 提示外没有空白错误。
- 已执行 `make -j2`：`src/main.c`（包含本次两处改动）成功编译为 `obj/server/main.o`，但链接
  `bin/jh-online-server.exe` 时被正在运行的服务端占用，系统返回 `Permission denied`。根据
  项目进程边界，未停止、替换或重启该服务；待用户手动释放进程后需要重新执行完整构建。
- 用户随后手动关闭服务端后，已重新执行 `make -j2`；链接 `bin/jh-online-server.exe` 成功，
  完整构建通过。

## 12. 失败接取后 NPC 感叹号消失（2026-08-04）

### 运行时证据

同一用户复现会话先正确收到场景候选：

```text
mock_task_candidates role=10001 scene=c00蓬莱仙岛_01 tasknum=1 ...
mock_npc_dialog actor=30001 ... task_offer=1 ... task_option_action=4 ...
mock_task_accept_backpack_full task=1 role=10001 given_item=807 given_count=3 ...
mock_task action=accept ... request_subtype=11 ... result=1 ... response_objects=3 ...
```

失败 `6/11` 后没有新的 `mock_task_candidates`。数据库没有创建活动任务，且运行时资格仍满足；
因此应继续显示感叹号，但客户端的场景候选缓存没有被恢复。

### 客户端契约与首次偏离

按 `binary_name=江湖OL.CBE` 动态选择 IDA 实例后，反汇编确认：

1. `net_handle_task_response_dispatch` (`0x0104726C`) 的 `6/11` case 首先读 `result`；当
   `result != 0` 时直接返回，不读 `taskinfo`，也不调用场景标识刷新。
2. 同函数的 `6/14` case 在 `action=0` 时读取 `tasknum/taskinfo`，重建可接任务候选，随后在
   `0x01047A76` 调用 `scene_refresh_interact_prompt_types` (`0x01017C6C`)。
3. `scene_refresh_interact_prompt_types` 将世界 NPC 的交互标识重新计算；候选记录的第二个
   字符串匹配场景节点名称时，`DeserializeRoleInfo` 记录会赋予正常的感叹号提示类型。

首次偏离因此不是 `6/11` 失败本身，也不是 NPC 配置或数据库状态，而是失败响应没有把客户端
因任务对话暂时移除的候选缓存恢复为服务端仍然权威的可接候选。

### 修复边界与计划

仅当这次 `6/11` 来自刚才 NPC 对话记录的 offer、任务资格在确认前仍然可用、且最终接取结果
为失败时，追加既有的 `6/14 action=0` 候选对象。候选对象继续由
`vm_net_mock_append_taskaction14_object()` 从当前角色、当前场景、MySQL 任务状态和动态 NPC
绑定重新生成；不复用旧缓存、不硬编码任务/角色/NPC，也不会在任务实际接取成功时错误恢复
感叹号。

### 实现与验证

- `src/server/mock_server_scene_sync.c` 现在在该窄范围失败条件成立时，在原 `6/11`、可选
  `25/5` 和已确认的满包 `25/11` 提示之后追加 `6/14 action=0`。
- 追加对象调用既有 `vm_net_mock_append_taskaction14_object()`，因此候选重新按当前持久化
  状态计算；如果任务在任何并发/写入异常后实际已经存在，生成器会返回零候选而不是显示错误
  的感叹号。
- 新增 `mock_task_accept_restore_candidates` 日志记录任务、角色、失败类型、是否带原始
  `25/5` 尾对象及最终对象数，便于手动复现时核对。
- `git diff --check` 仅报告仓库既有的 CRLF 转换提示；`make -j2` 已在 2026-08-04 通过。
