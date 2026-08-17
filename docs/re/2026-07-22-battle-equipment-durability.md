# 战斗结束装备耐久

## phase

战斗状态持久化；状态：已实现，纯状态回归通过，客户端黑盒待验收。

## 预期

一次已结束的战斗，无论以击败怪物、角色死亡或成功逃跑结束，角色当前穿戴
的每件装备各扣 1 点耐久；耐久为 0 时保持 0。扣减必须落入
account_role_equipment_durability，且同一场战斗的重复请求或重复结算不能
重复扣减。

## 已有数据与客户端契约

- 服务端的 account_role_equipment_durability 行按
  (account_id, role_id, slot_index) 保存 item_id、durability、
  durability_max。vm_net_mock_role_service_apply_battle_wear 已按
  g_mockBattleOperateSessionSerial 防重并对非空装备槽递减。
- mmGameMstarWqvga.cbm:sub_D04(0x00000D04) 解析 1/7/7 的 type、
  iteminfo；每行读取 seq、itemId、currentCount、common-extra。
  当 type=2 时，行经物品管理器 +104 插入装备列表；装备
  currentCount 是耐久。因此现有登录 bootstrap 1/7/7 type=2 是已确认的
  持久耐久加载路径。
- mmBattleMstarWqvga.cbm:HandleBattleSettleMsg(0x0000743C) 只读取
  4/7 的 exp、lastexp、curexp、persentexp、energy、energymax、gold、
  level、result、bagstatus、hp、mp、itemnum、iteminfo。它没有装备耐久字段。
  因而不能把未经验证的装备刷新对象塞进战斗结算包；当前任务的权威要求是
  正确持久化，客户端会在下一次原生装备 bootstrap 时读取最新耐久。

## 固件复核（2026-07-22）

本节区分已由客户端固件证明的协议事实和当前 mock 的玩法策略。

- mmBattleMstarWqvga.cbm:HandleBattleSettleMsg(0x0000743C) 只处理战斗
  结果、角色 HP/MP、经验、钱和掉落展示；它没有遍历装备列表，也没有本地
  对任一装备计数做减法。因此耐久不是客户端按回合或按结算自行扣除，而必须
  由原始服务端作为权威状态计算后下发。
- mmGameMstarWqvga.cbm:sub_11CE(0x000011CE) 对普通网络事件中的每个
  1/7/7 对象调用 sub_D04，没有登录专用分支。
  sub_D04(0x00000D04) 从 iteminfo 行读取
  seq、itemId、currentCount、common-extra；装备 ID 使用 equip.dsh，
  且 type=2 走装备管理器路径。该链路证明服务端具备单独同步装备当前值的
  通道，但尚未证明它可在已经装载同一槽位后安全地做原地替换。
- 主程序 HandleRepairResponse(0x01028C00) 的 1/7/29 只读取
  type、repairnum、coolmoney 并显示确认框；GBK 文本为“您身上有 N 件装备
  需要修理，花费 M 币”或“修理该装备需花费 M 币”。它是报价/确认阶段，
  不是战后耐久结算或已修复装备的刷新包。
- 固件中没有可用的“每战扣多少、哪些装备扣、逃跑或死亡是否扣”的常量或
  分支。每场每槽减 1 来自 mock 的玩法设定，不能标记为原版服务器规则。
- `equip.dsh` 列 19（`耐久`）是客户端本地显示的单件装备上限；例如 1001
  木制宽剑为 50，1101 桃木宽剑为 80。它不是战斗规则的直接证据，却是服务端
  创建、校正和修理 `currentCount` 时必须遵守的数据契约，不能以统一 100 代替。

结论：当前实现符合本任务给出的玩法要求，但不能称为“已还原原服耐久规则”。
尤其“成功逃跑/角色死亡也扣一次”的选择在逻辑上符合“战斗结束”，却仍缺少
原服包或服务端数据的直接证据。若目标改为严格还原，必须采集原服一次胜利、
失败/死亡和成功逃跑前后的装备 iteminfo，再比较每个装备行的
currentCount。

## 首个偏离

正常胜利的 `4/6 + 4/7` 路径先由
`vm_net_mock_append_battle_terminal_status_objects()` 构建结算对象。构建成功后，
调用方看到 `terminalStatusAppended=true`，会跳过旧的
`vm_net_mock_battle_save_terminal_role_state()`；而旧的耐久扣减只在该保存助手中，
所以正常胜利没有执行装备耐久扣减。死亡和成功逃跑走的是另一条当前状态保存路径，
也没有共享这个扣减边界。

这说明首个错误状态不是客户端 `4/7` 解析，而是服务端把耐久扣减错误地绑定在某个
结算保存分支上，导致不同终局的持久化行为不一致。

## 修复设计

保留 `lastBattleWearSerial` 的单场防重和 MySQL 持久化，并把扣减挂到已经成功追加
`4/7`（以及自动药品同步对象）的终局边界：

1. `vm_net_mock_append_battle_terminal_status_objects()` 成功追加 `4/7` 后调用
   `vm_net_mock_role_service_apply_battle_wear()`，覆盖普通胜利、组队胜利和战斗物品
   导致的终局；
2. 没有 `4/7` 的死亡、成功逃跑及零奖励终局继续由
   `vm_net_mock_battle_save_completed_current_role_state()` 或
   `vm_net_mock_battle_save_terminal_role_state()` 调用同一助手；
3. 保存失败时恢复本场原有 serial，允许后续同一终局重试，而不是把失败静默记成已扣；
4. 失败逃跑仍是进行中的战斗，不扣耐久；`4/7` 不新增未经固件证明的装备字段或
   刷新对象。

助手只处理可用、槽位匹配且耐久大于 0 的装备，每件减 1 并钳制到 0；日志同时记录
本场 serial 和实际扣减槽位数。

## 回归

`scripts/equipment-durability-max-regression.php` 使用真实 `equip.dsh` 上限验证修理
不会超过物品定义；`scripts/battle-equipment-durability-regression.c` 在不连接
MySQL、不启动服务、不操作客户端的纯状态夹具中验证战斗扣减助手。

1. 旧 44/100、70/100 记录在原生装备 bootstrap 前归一为 44/50、70/80；
2. `26/1 {type=2,id=0xe3000001}` 修理后分别为 50/50、80/80；
3. 铜钱只按真实缺口 6+10 扣除。

战斗结算路径使用同一 `lastBattleWearSerial` 防重助手；仍需用隔离测试账号完成
客户端黑盒场景，确认战斗结束后重新打开装备界面时，原生 `1/7/7 type=2` bootstrap
读取到持久化后的耐久。

运行结果（隔离服务）：

    equipment durability max regression passed repaired=50/50,80/80 money=984

修理回归不向 26/1 对话响应附加未经验证的装备替换对象；客户端仍会通过既有
1/7/7 type=2 装备 bootstrap 消费当前耐久。

本次战斗扣减纯回归输出：

    battle-equipment-durability-v1 passed: usable=44->43->0 broken/wrong-slot unchanged

构建验证：`make -j2` 成功链接 `bin/jh-online-server.exe`。
