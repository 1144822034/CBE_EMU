# 装备强化（WT 29/1、29/2、29/3）

Date: 2026-07-19

Status: 已实现，待客户端完整回归（预览、确认、重登）

## 1. 当前卡点

- 可见现象：在背包装备操作列表点击“强化”后，等待进度条不消失。
- 触发方式：背包 -> 装备 -> 强化。
- 本轮目标：实现强化界面初始化、材料预览和确认强化三阶段的服务端响应。

## 2. 运行时证据

- 触发：背包中选择装备，进入强化，放入五个一级玄晶后点击提交。
- `mock-service` 已确认先收到并正确处理 `1/29/1`：`seq=26`、物品 `20002`、响应
  `29/1` 为 287 字节；客户端能继续进入材料选择阶段。
- 紧接着收到单对象 `1/29/2`，总长 82 字节、对象 payload 长 73 字节；旧解析器拒绝该
  `occultinfo` 字段，dispatch 记录为
  `unhandled wt=29/2 len=82 objects=1 first=1/29/2:73`，server-only 端回包长度为零。
  这是加载条不消失的首次偏离，而非客户端 UI 或网络事件的后续症状。
- 原始 `29/2` payload（73 字节）已捕获：
  `08 equipseq 00 04 00 02 00 1A 0A occultinfo 00 2D`
  后接五条 `00 04 00000385 00 01 01`。其中目标装备序列是 `0x001A`（26），五条材料
  均为一级玄晶（901）、数量 1。
- `SendEquipSequenceReq(0x0101DD1E)` 的静态证据与实包一致：材料循环逐行调用
  `stream_write_i32_be_tagged` 和 `stream_write_i8_tagged`，因此每条材料严格为
  `00 04 <itemId:u32> 00 01 <count:u8>`（9 字节）。
- 根因：旧的通用数值扫描器把 `equipseq` entry 的外层长度 `00 04` 当成 `u32` 类型标记，
  把 `00 02 00 1A` 错读为 `0x0002001A`，超出 `u16` 范围后拒绝整个包。材料字段也被错误地
  当成可能含有嵌套 blob 长度的值。首个错误状态是 `1/29/2` 未被 dispatcher 处理，而非
  客户端等待界面。
- 修复：`29/2` 和 `29/3` 使用专属、精确的 tagged-entry 读取器解析 `equipseq`，并将
  `occultinfo` 当作直接的 9 字节 tagged 材料流；已删除取证日志和无证据的 5 字节材料兼容分支。

## 3. IDA 证据

| binary | function/address | findings |
| --- | --- | --- |
| `江湖OL.CBE` | `BattleAction_SelectSkill(0x0101CD1E)` | 构造 `1/29/1`，写入装备字段 `seq`。 |
| `江湖OL.CBE` | `SendEquipSequenceReq(0x0101DD1E)` | 构造 `1/29/2` 或 `1/29/3`，写入 `equipseq` 和 `occultinfo`。 |
| `江湖OL.CBE` | `SceneNodeCreateAndInit(0x0101DEDE)` | 强化界面模式 1 确认时发 subtype 2，模式 3 确认时发 subtype 3。 |
| `江湖OL.CBE` | `HandleItemUseAndEquip(0x01028C7C)` | 解析 `29/1..3` 响应并负责结束等待状态、刷新强化等级和消耗玄晶显示。 |

## 4. 请求 / 响应契约

### `1/29/1` 打开强化界面

- Request：`seq:u32`。
- Response：`result:u8`、`curlevel:u16`、`maxlevel:u16`、`num1:u8`、`data1:raw`、`num2:u8`、`data2:raw`。
- `data1` 是各强化等级需求值序列，`data2` 是各级玄晶提供值序列。

### `1/29/2` 预览强化结果

- Request：`equipseq:u32`、`occultinfo:raw`。
- `equipseq` entry：外层 `u16 length=4`，值为 `00 02 <u16 equipSeq>`。
- `occultinfo` entry：外层 `u16 length=9*n`，值为直接的 n 条
  `00 04 <u32 itemId> 00 01 <u8 count>`，其中 `1 <= n <= 5`；它不是嵌套 blob。
- Response：`result:u8`、`value:u32`（成功率）、`money:u32`（消耗铜币）。

### `1/29/3` 确认强化

- Request：同 `29/2`。
- 成功或失败 Response：`result` 为 1/2，并包含 `tnum:u8`、`equipseq:u16`、`occult:raw`。
- 错误结果：3 装备不存在，4 铜钱不足，5 玄晶不足，6 达到上限。

## 5. 服务端实现

- `vm_net_mock_build_equipment_enhance_response` 严格处理 `1/29/1..3`。
- 可用玄晶限定为物品 901..916（一级至十六级玄晶）。
- 预览阶段只计算玄晶强度、成功率和费用；确认阶段才扣除玄晶与角色铜币。
- 背包、网格和装备列表的 common-extra 前两个 `i16` 依次为**当前强化等级**和**强化等级上限**；客户端把第一个值写入 `item+286`，用于显示 `(+N)` 和装备属性计算。
- 日志来源名为 `builtin-equipment-enhance`，稳定事件名为 `mock_equipment_enhance`。

## 6. 持久化

- `account_role_backpack` 新增 `enhance_level SMALLINT UNSIGNED NOT NULL DEFAULT 0`。
- 新数据库由 `server/mysql/schema.sql` 和 payload 迁移建表脚本直接创建该列。
- 已有数据库需执行 `server/mysql/migrate_add_equipment_enhancement.sql`。

## 7. 验证清单

- [x] 请求 detector 仅命中单对象 `1/29/1..3`。
- [x] Windows 客户端与 server-only 翻译单元均编译通过。
- [x] 响应通过正常 mock-service 分发返回，不强写客户端状态。
- [x] 固定首次偏离为 `1/29/2.occultinfo` 解析拒绝，而非 `29/1`、客户端回调或持久化。
- [x] 以原始 82 字节 `29/2` 请求修正 `equipseq` 与 `occultinfo` 的正式解析，且移除取证探针。
- [ ] 客户端点击强化后进入强化界面且进度条消失。
- [ ] 选择玄晶后成功率和费用刷新。
- [ ] 强化完成后等级、玄晶数量和铜币在重登后保持一致。

## 8. 2026-07-27：提示“强化等级已达上限”的部署排查

### 触发与预期

- 触发：背包中选中未满级装备，进入“强化”。
- 实际：客户端提示“强化等级已达上限”。
- 预期：客户端应先收到 `1/29/1 { result=1, curlevel, maxlevel }`，再进入材料预览；只有持久化的该装备实例等级达到上限时，`1/29/3` 才可返回 `result=6`。

### 已确认的客户端与服务端契约

- `HandleItemUseAndEquip(0x01028C7C)` 对 `1/29/1.result=3` 显示打开强化页阶段的失败文本；对 `1/29/3.result=6` 显示“强化等级达到上限”。
- 原始 CBE 的 GBK 字符串确认 `1/29/3`：`4=金钱不足`（`0x01027B90`）、`5=玄晶不足`（`0x010296A4`）、`6=强化等级达到上限`（`0x010296B0`）。
- 因而不能通过调整客户端提示、忽略结果或降低客户端等级来处理；必须确认运行服务端读取的实例等级及其部署版本。

### 首个偏离与证据

- 2026-07-27 运行中的 `bin/jh-online-server.exe` 启动于 10:59:22，二进制常量仍为 `VM_NET_MOCK_ROLE_DB_VERSION = 5`。
- 当前源码已为版本 `6`，并包含装备实例的 `enhance_level`、耐久和关系表迁移；其服务端对象已在 19:13 重新编译，但可执行文件因旧进程占用没有完成链接替换。
- 运行时服务 stdout 未被重定向，所以本次 `mock_equipment_enhance phase=... level=... result=...` 证据未保留。这是无法判断某一具体装备是否真的为 16 级的直接原因。

首次部署问题修复后，实际复现记录为：`guest00001` 的序号 1、物品 1001 在 `29/1` 和 `29/2` 均为 `level=0,result=1`，确认阶段因角色仅有 26 铜钱、而费用为 100，返回 `result=6, reason=money-insufficient`。首个业务偏离是服务端误把内部错误顺序直接当作客户端结果码：`money-insufficient` 应发 `4`，却发成 `6`，所以客户端显示了“强化等级达到上限”。同一错误还使玄晶不足和实际满级分别错发为 `4`、`5`。

修正：增加单一错误码映射层，固定 `4=铜钱不足、5=玄晶不足、6=满级`；打开强化页的既有 `29/1` 特殊码保持 `2=装备不存在、3=满级`。这修复的是最早的协议语义偏差，不改变角色等级、材料、金钱或客户端内存。

### 修正与验证

- 已停止旧进程、执行 `make -j2`，并从新生成的 `bin/jh-online-server.exe` 重启服务；当前二进制已确认 `VM_NET_MOCK_ROLE_DB_VERSION = 6`。
- 错误码映射修正后再次执行 `make -j2` 并重启；当前监听 19090/19091 的进程只运行这一新二进制，健康检查仍为 `{"ok":true,"service":"jianghu-admin"}`。
- 新进程启动日志确认 `equipment_instance_schema_prepare storage=relational`，`account_role_backpack` 与 `account_role_equipment` 均具备 `enhance_level`、`durability`、`durability_max` 列。
- 管理健康检查 `GET http://127.0.0.1:19091/healthz` 返回 `{"ok":true,"service":"jianghu-admin"}`；19090/19091 由同一新进程监听。
- `actorinfo-attribute-contract-regression`、`equipment-stat-bonus-regression`、`trade-equipment-instance-regression`、`php -l scripts/equipment-durability-max-regression.php` 以及 `git diff --check` 均通过。
- 新服务日志为 `tmp/jh-online-server-current.stdout.log`。重新触发一次强化后，应先检查其中的 `mock_equipment_enhance phase=1/3` 行，再决定是否存在具体持久化实例被真实写成 16 级的问题。
- `equipment-enhancement-result-regression` 已通过，固定验证 `29/1` 与 `29/3` 所有拒绝码；它以原始客户端文字地址为证据，防止服务端内部枚举顺序再次泄漏到线上协议。

## 9. 2026-07-27：商城返回后强化等级消失

### 可重复触发

1. 角色同时持有一把装备中的木制宽剑（装备栏第 0 格）和一把背包中的木制宽剑。
2. 从背包操作菜单强化后者至 `+7`（本次运行记录最终成功到 `+8`）。
3. 打开商城，再返回场景并查看装备；背包中的木制宽剑不再显示该强化等级。

预期是同一件背包装备在任何列表重建后仍显示其持久化等级；商城不是强化状态的权威来源，不能靠商城返回时重写或镜像装备属性解决。

### 完整链路与首次偏离

- `1/29/1..3` 的入口是背包“装备 -> 强化”。`0x0101CD1E` 发送背包行的 `seq`，服务端在 `vm_net_mock_build_equipment_enhance_response` 中正确查找并写回该背包实例。
- 本次 MySQL 数据证明背包木制宽剑的 `enhance_level=8` 已持久化。最初的序号冲突修复后，该实例迁移为 `seq=9`；这排除了强化写入丢失或商城扣除。后续重登复现进一步证明此前 `30/21` / `17/1` 虽携带了 `8`，却写在 common-extra 的第二个 `i16`，并不满足客户端显示字段的契约；该字段语义错误在本节末尾的第 10 节单独记录。
- 同一角色的装备栏第 0 格也使用客户端固定身份 `seq=slot+1=1`，其 `enhance_level=0`。商城返回会再次依次下发背包列表和 `7/7 type=2` 装备列表，使两个客户端物品对象共享 `seq=1`。
- `UpdateTaskProgressEntry(0x01028726)` 是 `1/29/3` 成功后的客户端更新路径；它只按 `item+276` 的 `seq` 找到第一个对象，**不按背包/装备类别过滤**。故服务器首次违反的是物品实例序号命名空间：背包 `seq=1` 与装备栏 `seq=1` 同时存在，客户端的原生全局匹配变得歧义，后续列表重建显示错误的那一件。

已排除：不能把背包强化改写到装备栏、不能停止商城列表重建、不能让两把剑共享强化等级，也不能改写客户端内存；这些都会掩盖而非修复首个协议身份冲突。

### 修正

- 固定保留 `1..8` 给装备栏 `slot+1` 身份；背包新实例从 `9` 起分配。
- 背包规范化在角色加载时迁移旧的 `seq<=8` 行到空闲的背包序号，保持物品 ID、数量、强化等级和耐久不变，并立即通过既有 MySQL 全量事务保存。
- 所有服务端背包实例来源（装备卸下、特殊物品给予、普通物品给予、交易收货）统一通过同一分配器，避免未来重新产生碰撞；分配器处理 `u16` 回绕且不落入保留区。
- 迁移仅在检测到旧保留序号时记录一次 `backpack_sequence_namespace_migrate`，用于核对持久化修复，不引入高频包日志。

### 验证

- [x] `backpack-sequence-namespace-regression`：构造背包 `seq=1/2`，规范化后得到 `seq=9/10,next=11`，且木制宽剑 `+7` 保持不变。
- [x] 编译后启动服务，`role=10001` 的 MySQL 背包行已为 `seq=9..14`，其中木制宽剑仍为 `enhance_level=8`；装备栏第 0 格仍为固定 `seq=1,enhance_level=0`。
- [ ] 原始客户端路径：强化背包木制宽剑，打开商城并返回；背包内同一实例仍显示强化等级。
- [ ] 相邻路径：重登、装备卸下、交易收取和新增背包物品均不产生 `1..8` 背包序号。

## 10. 2026-07-27：重登后强化值仅在点击“强化”后出现

### 触发、预期与实际

1. 将背包中的木制宽剑强化到 `+8`，确认 MySQL `enhance_level=8`。
2. 返回标题并重新登录角色，打开背包：该剑不显示 `+8`。
3. 对同一背包序号点击“强化”：`1/29/1` 正确返回 `curlevel=8`，客户端随即显示 `+8`。

预期是任何列表重建都应直接显示持久化等级；`1/29/1` 只能初始化强化界面，不能成为背包显示的额外修补通道。

### 完整链路、证据与首次偏离

- 运行日志记录重登期间依次下发 `30/21` 背包网格、`7/7 type=2` 装备列表；随后背包页 `17/1` 返回 `rows=6,iteminfo_len=105`。对 `seq=9,item=1001` 的 `1/29/1` 记录为 `level=8,result=1`，所以持久化和强化请求查找均正确。
- `江湖OL.CBE:ParseEquipAttributes(0x010185C2)` 顺序读取两个 `i16` 和一个 `u8 attr-count`。`mmGameMstarWqvga.cbm:sub_418C` 将前两个值写入 `item+286/+287`；背包渲染路径在 `tmp/ida_full_mmgame_actor_update/decompiled.c` 中以 `item+286` 生成 `%s(+%d)`。主程序 `CalcEquipStatBonus` 也把 `item+286` 作为强化等级。
- 修改前，服务端共同 helper 写入 `{ stackRuntimeByte, enhanceLevel, 0 }`。对该剑实际是 `{1,8,0}`：客户端读取后得到“当前 `+1`、上限 `8`”，而非“当前 `+8`、上限 `16`”。`1/29/1` 的专用 parser 再单独将 `curlevel=8` 写到 `item+286`，因此产生“点击强化才显示”的表象。
- 首次违反的契约位于所有列表响应共用的 `vm_net_mock_seq_put_item_common_extra`，不是 MySQL、序号分配、商城返回时序或客户端状态。已排除按界面请求补发 `29/1`、重写客户端内存或按物品 ID 特判，因为都不能使正常列表 parser 获得正确状态。

### 修正与验证

- common-extra 统一固定为 `{ currentEnhanceLevel, maxEnhanceLevel, attrCount }`；装备上限由 `equip.dsh` 分类确定为 `16`，非装备为 `0`。
- 修正了所有使用该 helper 的列表及预览路径：`17/1` 背包、`30/21` 登录网格、`7/7 type=2` 已装备列表、商城列表/页、任务奖励增量、交易预览与附近玩家装备预览。数量仍只从各自明确的 `count` 字段传输，不再复用强化扩展字段。
- 定向夹具 `tmp/item-common-extra-enhancement-regression.c` 构造同一件 `seq=9,+8` 装备，断言 `17/1`、`30/21` 和 `7/7 type=2` 都序列化为“当前 `8`、上限 `16`”。
