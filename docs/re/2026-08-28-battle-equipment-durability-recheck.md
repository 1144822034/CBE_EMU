# 战斗后装备耐久复查（2026-08-28）

Date: 2026-08-28

Status: implementation ready; real client restart/retest pending

## 1. 当前卡点

- 可见现象：player-1 完成数场战斗后，头盔仍显示 `539/540`。
- 触发方式：正常完成一场怪物战斗后观察已穿戴装备的当前耐久。
- 本轮最小目标：区分“服务端没有写入耐久”与“服务端已写入、客户端仍显示旧装备实例”。

## 2. 运行时证据

- player-1 的本次 `bin/server_out.txt` 已记录三次正常胜利及耐久持久化：
  - battle `1`：`mock_role_db_mysql_save ... reason=battle-equipment-durability-wear`，随后
    `mock_equipment_durability_wear role=10871 battle=1 amount=1 slots=8`（第 273--274 行）；
  - battle `2`：同一成功保存与 `battle=2 amount=1 slots=8`（第 305--306 行）；
  - battle `3`：同一成功保存与 `battle=3 amount=1 slots=8`（第 358--359 行）。
- 三场均有 `mock_battle_settle ... victory=1`，随后是唯一的本场耐久保存；未出现
  `mock_equipment_durability_wear_store`，也未出现 `mock_battle_insight_auto_repair`。
- `amount=1` 表示每件可用已穿戴实例扣一，`slots=8` 表示本场的八件有效装备都已写入。
  因而若头盔进入这三场时为 `540/540`，权威耐久至少应依次经过 `539`、`538`、`537`；
  仍显示 `539/540` 精确表明客户端停留在第一次更新后的实例快照。
- 每场结算后，客户端只发出原生空 `WT 25/5` 关闭奖励面板；服务端记录为
  `net_send ... wt=25/5 len=9 ... resp=23`（battle 1：第 282--284 行；battle 2：
  第 313--315 行；battle 3：第 366--368 行）。其后直接进入移动／下一场交互，
  没有原生 `7/7(type=2/type=3)` 装备重载请求。

## 3. IDA 目标

| binary | function/address | reason | findings |
| --- | --- | --- | --- |
| `mmBattleMstarWqvga.cbm` | `HandleBattleSettleMsg (0x0000743C)` | 验证战后客户端是否自行扣耐久 | 已有取证表明它只读取 `4/7` 的经验、HP/MP、金钱、结果与掉落字段，不读取装备耐久。 |
| `mmGameMstarWqvga.cbm` | `sub_D04 (0x00000D04)` | 验证装备当前耐久的原生加载边界 | `1/7/7 type=2.iteminfo` 的装备行读取当前计数作为耐久；该对象在首次登录装备 bootstrap 中建立装备实例。 |
| `江湖OL.CBE` | `HandleItemOperationResponse (0x01033544)` | 验证已存在装备能否战后原地更新 | subtype `11/12` 按 `seq` 查找活动物品；对类别 `<10` 或 `15` 的行将 `info` 中的 `u32` 当前值写入 `item+272`。类别 15 即装备，因此这是已验证的耐久原地更新通道。 |

本轮复核了项目内已导出的 `0x01033544` 反编译：其 `7/11` 分支逐行读取
`info` 中的 `row_count, seq, value`，经物品管理器 `+84` 按序号取得已有实例。物品
id 为 802/803 时仍走药瓶的专用字段；其他类别会查看 `item+282`，类别 15 明确把
`value` 写到 `item+272`。这与装备 bootstrap 使用的当前耐久字段一致。

## 4. 调用链 / 业务流程

1. 普通胜利在 `vm_net_mock_append_battle_terminal_status_objects()` 成功追加原生 `4/7` 后，调用 `vm_net_mock_role_service_apply_battle_wear()`；死亡和成功逃跑由 `vm_net_mock_battle_save_completed_current_role_state()` 走同一助手。
2. 助手以 `g_mockBattleOperateSessionSerial` 与角色服务状态的 `lastBattleWearSerial` 防重，仅对槽位匹配且当前耐久大于零的已穿戴实例减一。
3. 该变更通过 `vm_net_mock_role_db_save("battle-equipment-durability-wear")` 事务写入；失败时恢复角色实例、服务状态和本场防重序号。
4. `4/7` 不携带装备耐久，客户端结算 parser 也不更新装备实例。当前耐久的已验证客户端载入边界是原生 `1/7/7 type=2` 装备 bootstrap。
5. 新的结算尾部在上述扣减完成后追加一个 `1/7/11 { info }`：每个可用装备槽一行，
   `seq=slot+1`（与 bootstrap 相同）且 `value=已扣减后的 durability`。该对象不创建
   新物品，也不会走 `7/7 type=2` 的插入／操作分支。

## 5. 结构体 / 状态字段笔记

- owner: `vm_net_mock_role_state.equippedItems[slot]`
- current value: `durability`；上限为 `durabilityMax`。
- mirror: `vm_net_mock_role_service_state.durability[slot]` 与 `durabilityMax[slot]`。
- idempotency: `lastBattleWearSerial` 必须不同于本场 `g_mockBattleOperateSessionSerial`。
- persistence owner: relational `account_role_equipment` 实例行；旧的独立耐久表只用于兼容迁移。
- confidence: 服务端状态变更与持久化回滚路径均已由源码和纯状态夹具确认；客户端在战后立即原地刷新装备实例尚未确认。

## 6. 请求 / 响应契约

### Request

- WT: 战斗操作 / 终局相关请求，具体 subtype 取决于普通攻击、技能、逃跑或死亡路径。
- completion condition: 服务端只在真实终局追加 `4/7` 或走已完成的死亡／成功逃跑状态保存；失败逃跑不扣耐久。

### Response

- WT: `4/7` 战斗结算对象，常与 `4/6` 动作对象同包。
- fields: 经验、HP/MP、金钱、结果、背包／掉落等；没有装备耐久字段。
- equipment refresh: 已验证为独立的 `1/7/7 { type=2, iteminfo }` 装备实例流，不能在没有客户端 parser 证据时附加到 `4/7` 专用回调。
- battle-close boundary: 原生 `25/5` 只用于 `BattleScene_ExitAndCleanup(0x60C8)` 释放
  普通战斗状态。它不是装备重载请求；本轮真实日志也没有在其后看到客户端自己发出
  `7/7(type=2/type=3)`。已有固件证据表明把 `type=2` 行主动塞进非该请求的回包会进入
  物品操作／插入生命周期，并已有 `MMORPG_Screen_InGame.c:913` 断言的反例。
- implemented refresh: 结算包可安全追加 `1/7/11 { info }`。实际 player-1 运行已经证明
  同包 `4/7+7/11` 用于自动药瓶；`0x01033544` 同一分支对类别 15 写 `item+272`，因此
  该对象的装备行复用已存在实例并原地更新耐久，而不是伪造新的 `7/7` 装备对象。

## 7. 成功路径与失败路径

### Success path

- 服务端日志出现本场唯一的 `mock_equipment_durability_wear`，且 `slots` 等于本场可用已穿戴装备数。
- 隔离数据库中同一角色的 `account_role_equipment.durability` 在战前、战后精确减少一；若战斗心得未启用，不应被后续自动修理恢复。
- 同一个 `4/6 + 4/7` 终局响应尾部出现
  `mock_battle_equipment_durability_refresh ... response=4/7+7/11`；客户端的既有类别-15
  实例按相同槽位序号收到最新耐久。重新进入装备 bootstrap 仍会读取同一持久值。

本次已验证到第二个分支：服务端每场写入成功而 UI 不变；首次偏离是战后没有一个已验证的
客户端装备实例更新契约，而不是耐久结算或数据库事务。

### Failure path

- 若出现 `mock_equipment_durability_wear_store`，事务失败后内存和数据库都必须保持战前值。
- 本次已确认写库日志连续存在、界面停在 `539/540`；首次偏离是客户端装备实例刷新，而不是战斗扣减。
- 若没有任何耐久日志，则应首先检查该战斗是否真到达终局、是否是失败逃跑，或是否被其他终局路径绕开。

## 8. Negative Evidence

- 已执行 `make -j2`，当前工作区构建通过。
- 使用当前分文件测试包含方式编译并运行 `battle-equipment-durability-regression`，输出
  `battle-equipment-durability-v3 passed: usable=44->43->0 battle-7/11=43 bootstrap=43 broken/wrong-slot unchanged`。
  该夹具不启动监听器、不连接数据库或客户端；除实例扣减、零值钳制和错误槽位排除外，
  还验证战斗结算的类别-15 `7/11` 行按 `seq=1,current=43` 编码，以及下一次客户端原生
  `7/7(type=2)` 装备 bootstrap 同样编码 `currentCount=43`。
- 当前环境未配置 `CBE_AUTOMATION_MYSQL_PASSWORD`、自动化数据库名或自动化 PHP；不能在不触碰
  用户 `jh_online` 数据库的情况下启动隔离端到端场景，因此没有改连该数据库作为替代。
- 没有可用的同场真实战斗日志或隔离账号写库前后快照；因此不能将纯状态回归当成用户现象已经修复的证明。

## 9. Unknowns / Hypotheses

- resolved: 用户观察的是战后客户端的旧装备实例，而非未扣的服务端耐久。
  - evidence: 三场胜利均有成功的 `battle-equipment-durability-wear` 保存；UI 恰停在首场后的 `539/540`。
  - why it matters: 向 `4/7` 附加装备对象仍不符合已知专用 callback 契约，不能用该方式掩盖刷新缺口。
  - implementation: `0x01033544` 证明 `7/11` 正是可安全更新这份已存在实例的通道；新结算尾部
    已发送全部可用槽位的绝对耐久。仍需在重启后的实际客户端确认 UI 每战刷新。
- resolved for this run: 战斗心得自动修理没有发生；三场相邻终局均未出现
  `mock_battle_insight_auto_repair`。

## 10. 本轮实现

- `src/server/mock_server_battle.c` 新增
  `vm_net_mock_append_battle_equipment_durability_counts_object()`：扣减持久化完成后，以一个
  `7/11.info` 流同步每个可用已装备槽的 `(slot+1, durability)`。
- 该对象与已有的战斗自动药瓶 `7/11` 同属 `HandleItemOperationResponse(0x01033544)` 的已验证
  通道；装备类别 15 会更新 `item+272`，不会新增物品或重置战斗／场景状态。
- 未改变宿主网络投递、CBE 内存、寄存器、PC/LR 或战斗回调；响应仍由固件登记的普通
  网络 callback 消费。

## 11. 验证清单

- [x] 已审阅战斗结算、耐久持久化和登录装备 bootstrap 的既有 parser 证据。
- [x] 当前工程 `make -j2` 通过。
- [x] 纯状态耐久扣减回归通过。
- [x] player-1 真实客户端完成三场终局，日志证明每场耐久成功写入。
- [x] 首次偏离锁定为战后客户端装备实例刷新缺口。
- [x] 纯状态回归覆盖扣减后的下一次已验证装备 bootstrap 会编码最新耐久。
- [x] 已确认并实现由 battle callback 安全消费的 `7/11` 原生装备耐久刷新契约。
- [ ] 使用新构建重启服务后，以 player-1 完成一场普通胜利并确认头盔立即从当前值减一。
- [x] 没有强写客户端全局状态、寄存器、回调或响应字节。
- [x] 结果已回写到本文件。
