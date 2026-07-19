# 装备强化（WT 29/1、29/2、29/3）

Date: 2026-07-19

Status: implemented（等待客户端运行验证）

## 1. 当前卡点

- 可见现象：在背包装备操作列表点击“强化”后，等待进度条不消失。
- 触发方式：背包 -> 装备 -> 强化。
- 本轮目标：实现强化界面初始化、材料预览和确认强化三阶段的服务端响应。

## 2. 运行时证据

- 用户确认点击强化后永久等待。
- 本轮开始时没有可用的最新 `net_trace.log` / `net_packets.log` 请求样本，因此请求长度仍待运行时回填。
- 该缺口由客户端请求构造函数和响应解析器的静态证据约束，处理器只匹配单对象 `1/29/1..3`，不使用宽泛兜底。

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
- `occultinfo` 每行由 `u32 itemId + u8 count` 的 typed sequence 编码组成，最多五行。
- Response：`result:u8`、`value:u32`（成功率）、`money:u32`（消耗铜币）。

### `1/29/3` 确认强化

- Request：同 `29/2`。
- 成功或失败 Response：`result` 为 1/2，并包含 `tnum:u8`、`equipseq:u16`、`occult:raw`。
- 错误结果：3 装备不存在，4 玄晶不足，5 达到上限，6 铜币不足。

## 5. 服务端实现

- `vm_net_mock_build_equipment_enhance_response` 严格处理 `1/29/1..3`。
- 可用玄晶限定为物品 901..916（一级至十六级玄晶）。
- 预览阶段只计算玄晶强度、成功率和费用；确认阶段才扣除玄晶与角色铜币。
- 强化成功后通过背包 common-extra 的第二个 `i16` 返回等级。
- 日志来源名为 `builtin-equipment-enhance`，稳定事件名为 `mock_equipment_enhance`。

## 6. 持久化

- `account_role_backpack` 新增 `enhance_level SMALLINT UNSIGNED NOT NULL DEFAULT 0`。
- 新数据库由 `server/mysql/schema.sql` 和 payload 迁移建表脚本直接创建该列。
- 已有数据库需执行 `server/mysql/migrate_add_equipment_enhancement.sql`。

## 7. 验证清单

- [x] 请求 detector 仅命中单对象 `1/29/1..3`。
- [x] Windows 客户端与 server-only 翻译单元均编译通过。
- [x] 响应通过正常 mock-service 分发返回，不强写客户端状态。
- [ ] 客户端点击强化后进入强化界面且进度条消失。
- [ ] 选择玄晶后成功率和费用刷新。
- [ ] 强化完成后等级、玄晶数量和铜币在重登后保持一致。
- [ ] 补充真实请求长度及服务端命中日志。
