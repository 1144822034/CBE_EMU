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
- 错误结果：3 装备不存在，4 玄晶不足，5 达到上限，6 铜币不足。

## 5. 服务端实现

- `vm_net_mock_build_equipment_enhance_response` 严格处理 `1/29/1..3`。
- 可用玄晶限定为物品 901..916（一级至十六级玄晶）。
- 玄晶强度（29/1 data2，紧凑线性表，避免客户端截断 maxlevel）：
  `power = 玄晶等级 × 100`；`required(data1)`：`+0→100`，`+N→N×250`。
  指数表曾导致 +12 后提示上限（`2026-07-31-enhance-maxlevel-clamp-at-12.md`）。
- 成功率（权威：29/2 `value` / 29/3 掷骰）：
  - `+L→+(L+1)`：**目标级及以上**（`tier≥L+1`）→ 100%；**当前级** → 40%；再低一级 ×40%
  - 例：`+4→+5` 五级晶 100%，四级晶 40%
  - `rate = min(100, Σ unit(tier)*count)`
  - 详见 `2026-07-31-enhance-same-tier-rate-100.md`
- 铜币：与 compact `required` 同曲线
- 预览阶段只计算玄晶强度、成功率和费用；确认阶段才扣除玄晶与角色铜币。
- 强化成功后通过背包 common-extra 的**第一** `i16` 返回当前等级（第二为 maxlevel；见 `2026-07-29-login-backpack-enhance-zero.md`）。
- 日志来源名为 `builtin-equipment-enhance`，稳定事件名为 `mock_equipment_enhance`。

## 6. 持久化

- `account_role_backpack` 有 `enhance_level`（背包装备实例）。
- `account_role_equipment` 有 `enhance_level`（穿戴槽）；穿戴/卸载必须在两侧
  之间搬运，否则会表现为「强化好的装备卸下后等级丢失」。
  见 `2026-07-27-equip-unequip-lose-enhance.md`。
- 新库由 `server/mysql/schema.sql` 建列；旧库执行
  `migrate_add_equipment_enhancement.sql`（背包）与
  `migrate_add_equipped_enhance_level.sql`（穿戴），或依赖服务启动时的
  `ALTER` 探测。

## 7. 验证清单

- [x] 请求 detector 仅命中单对象 `1/29/1..3`。
- [x] Windows 客户端与 server-only 翻译单元均编译通过。
- [x] 响应通过正常 mock-service 分发返回，不强写客户端状态。
- [x] 固定首次偏离为 `1/29/2.occultinfo` 解析拒绝，而非 `29/1`、客户端回调或持久化。
- [x] 以原始 82 字节 `29/2` 请求修正 `equipseq` 与 `occultinfo` 的正式解析，且移除取证探针。
- [ ] 客户端点击强化后进入强化界面且进度条消失。
- [ ] 选择玄晶后成功率和费用刷新。
- [ ] 强化完成后等级、玄晶数量和铜币在重登后保持一致。
- [x] 确认强化后背包玄晶/铜币同步：`29/3` 追加 `7/7 type=2`+`7/11`+`10/26`
- [x] 确认强化成功后主背包装备强化等级：追加装备行 `7/7 type=2`+`7/11`（见 `2026-07-29-enhance-level-backpack-zero.md`）
  （见 `2026-07-29-enhance-crystal-bag-count-stale.md`）。
- [ ] 选晶放入时选择列表即时刷新：客户端 `0x900`/`0x580` 分裂，服务端无 place 包。
