# 战斗后装备耐久复查（2026-08-28）

Date: 2026-08-28

Status: investigating

## 1. 当前卡点

- 可见现象：用户报告完成一场战斗后，装备界面上的耐久没有变化；近期尚未复测。
- 触发方式：正常完成一场怪物战斗后观察已穿戴装备的当前耐久。
- 本轮最小目标：区分“服务端没有写入耐久”与“服务端已写入、客户端仍显示旧装备实例”。

## 2. 运行时证据

- `bin/server_out.txt` 中当前角色 `10871` 的最近记录只覆盖登录装备初始化：
  `mock_equipment_login` 和 `mock_login_equipment_type3_completion`；没有同一运行的
  `mock_equipment_durability_wear`、`mock_battle_terminal_save` 或写库失败记录。
- 因此现有日志不能证明用户报告所述那场战斗是否抵达正常终局，也不能证明写库结果。
- 当前源码的成功写库会记录
  `mock_equipment_durability_wear role=<id> battle=<serial> amount=1 slots=<n>`；写库失败则会记录
  `mock_equipment_durability_wear_store` 并恢复扣减前的内存状态。

## 3. IDA 目标

| binary | function/address | reason | findings |
| --- | --- | --- | --- |
| `mmBattleMstarWqvga.cbm` | `HandleBattleSettleMsg (0x0000743C)` | 验证战后客户端是否自行扣耐久 | 已有取证表明它只读取 `4/7` 的经验、HP/MP、金钱、结果与掉落字段，不读取装备耐久。 |
| `mmGameMstarWqvga.cbm` | `sub_D04 (0x00000D04)` | 验证装备当前耐久的原生加载边界 | `1/7/7 type=2.iteminfo` 的装备行读取当前计数作为耐久；该对象在首次登录装备 bootstrap 中建立装备实例。 |

本轮未新增 IDA 观察；以上地址和字段读取顺序来自
`docs/re/2026-07-22-battle-equipment-durability.md` 的既有固件取证。

## 4. 调用链 / 业务流程

1. 普通胜利在 `vm_net_mock_append_battle_terminal_status_objects()` 成功追加原生 `4/7` 后，调用 `vm_net_mock_role_service_apply_battle_wear()`；死亡和成功逃跑由 `vm_net_mock_battle_save_completed_current_role_state()` 走同一助手。
2. 助手以 `g_mockBattleOperateSessionSerial` 与角色服务状态的 `lastBattleWearSerial` 防重，仅对槽位匹配且当前耐久大于零的已穿戴实例减一。
3. 该变更通过 `vm_net_mock_role_db_save("battle-equipment-durability-wear")` 事务写入；失败时恢复角色实例、服务状态和本场防重序号。
4. `4/7` 不携带装备耐久，客户端结算 parser 也不更新装备实例。当前耐久的已验证客户端载入边界是原生 `1/7/7 type=2` 装备 bootstrap。

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

## 7. 成功路径与失败路径

### Success path

- 服务端日志出现本场唯一的 `mock_equipment_durability_wear`，且 `slots` 等于本场可用已穿戴装备数。
- 隔离数据库中同一角色的 `account_role_equipment.durability` 在战前、战后精确减少一；若战斗心得未启用，不应被后续自动修理恢复。
- 重新进入已验证的装备 bootstrap 后，客户端 `type=2` 行和装备界面读取同一持久值。

### Failure path

- 若出现 `mock_equipment_durability_wear_store`，事务失败后内存和数据库都必须保持战前值。
- 若写库日志存在、数据库已减一、界面不变，则首次偏离是客户端装备实例刷新，而不是战斗扣减。
- 若没有任何耐久日志，则应首先检查该战斗是否真到达终局、是否是失败逃跑，或是否被其他终局路径绕开。

## 8. Negative Evidence

- 已执行 `make -j2`，当前工作区构建通过。
- 使用当前分文件测试包含方式编译并运行 `battle-equipment-durability-regression`，输出
  `battle-equipment-durability-v1 passed: usable=44->43->0 broken/wrong-slot unchanged`。
  该夹具不启动监听器、不连接数据库或客户端，只证明实例扣减、零值钳制和错误槽位排除。
- 当前环境未配置 `CBE_AUTOMATION_MYSQL_PASSWORD`、自动化数据库名或自动化 PHP；不能在不触碰
  用户 `jh_online` 数据库的情况下启动隔离端到端场景，因此没有改连该数据库作为替代。
- 没有可用的同场真实战斗日志或隔离账号写库前后快照；因此不能将纯状态回归当成用户现象已经修复的证明。

## 9. Unknowns / Hypotheses

- unknown: 用户观察的是服务器的持久耐久，还是战后立即打开的客户端旧装备实例。
  - current guess: 后者优先级更高，因为 `4/7` parser 不读取耐久，但尚未用本轮实际战斗验证。
  - why it matters: 向 `4/7` 附加装备对象不符合已知专用 callback 契约，可能重现已有断言。
  - next probe: 以隔离测试账号保存战前／战后 SQL 行、服务端终局日志及客户端后续 `1/7/7` 请求／type-2 行。
- unknown: 战斗心得（828）是否在用户测试角色上有效。
  - current guess: 若有效，当前设计会先正常扣一，再按修理费用自动修回；表面上耐久不变是预期结果。
  - next probe: 同时记录本场是否出现 `mock_battle_insight_auto_repair`。

## 10. 本轮实现计划

- 计划改动：无生产代码改动。
- 目标文件：仅本调查记录。
- 原因：尚未锁定用户报告对应的首次偏离；现阶段不能通过伪造战后装备刷新或更改扣减规则试图消除现象。

## 11. 验证清单

- [x] 已审阅战斗结算、耐久持久化和登录装备 bootstrap 的既有 parser 证据。
- [x] 当前工程 `make -j2` 通过。
- [x] 纯状态耐久扣减回归通过。
- [ ] 在隔离端口、隔离数据库和标记测试账号上复现一场真实客户端终局。
- [ ] 记录请求、终局日志、耐久写库前后行与客户端后续装备 bootstrap。
- [x] 没有强写客户端全局状态、寄存器、回调或响应字节。
- [x] 结果已回写到本文件。
