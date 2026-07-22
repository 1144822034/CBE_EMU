# 战斗结束装备耐久

## phase

战斗状态持久化；状态：已实现并已验证。

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
- 固件中没有可用的“每战扣多少、哪些装备扣、逃跑或死亡是否扣、耐久上限”
  常量或分支。100 上限及“每场每槽减 1”来自 mock 的玩法设定，不能标记为
  原版服务器规则。

结论：当前实现符合本任务给出的玩法要求，但不能称为“已还原原服耐久规则”。
尤其“成功逃跑/角色死亡也扣一次”的选择在逻辑上符合“战斗结束”，却仍缺少
原服包或服务端数据的直接证据。若目标改为严格还原，必须采集原服一次胜利、
失败/死亡和成功逃跑前后的装备 iteminfo，再比较每个装备行的
currentCount。

## 首个偏离

vm_net_mock_role_apply_battle_settlement 调用了
vm_net_mock_role_service_apply_battle_wear，而它只会在胜利
4/7 结算路径执行。

- 击败怪物：4/6 + 4/7 -> vm_net_mock_battle_save_terminal_role_state
  -> vm_net_mock_role_apply_battle_settlement，会扣一次。
- 成功逃跑：4/4 result=1 -> vm_net_mock_battle_save_current_role_state，
  不经过 4/7，原先不会扣。
- 角色死亡：敌方动作使 HP 变 0 -> 同一 save_current_role_state，
  原先也不会扣。

因此扣耐久被错误归属给“胜利奖励结算”而非“战斗已结束”。

## 修复设计

保留现有 lastBattleWearSerial 防重和 MySQL 行持久化，但将调用从奖励结算
移到终结状态拥有者：

1. vm_net_mock_battle_save_terminal_role_state：胜利结算完成后扣一次；
2. 角色死亡的每个终结分支：保存角色当前状态后扣一次；
3. 成功逃跑分支：发送 4/4 result=1、结束战斗后扣一次。

失败逃跑仍是进行中的战斗，不能扣耐久。4/7 不新增未知字段或伪造装备
刷新包。

## 回归

tmp/battle-equipment-durability-regression.php 在隔离端口创建一件耐久 100 的
武器，验证：

1. 一次击败怪物后为 99；
2. 第二场成功逃跑后为 98；
3. 每个结算响应均使用原有 4/7（胜利）或 4/4 result=1（逃跑）契约。
4. 失败逃跑仍在同一场战斗内，保持原耐久。

角色死亡分支与这两个路径使用相同的 lastBattleWearSerial 防重助手，并由
源代码分支审查覆盖；后续可在有稳定强制死亡怪物数据时补充黑盒回归。

运行结果（隔离服务）：

    mock_equipment_durability_wear role=59224 battle=1 amount=1
    mock_equipment_durability_wear role=59224 battle=2 amount=1
    battle equipment durability regression passed victory=99/99 escape=98/98
    battle equipment durability regression passed failed-escape=100/100

失败逃跑只返回原有 4/4 result=0 和敌方行动，未出现
mock_equipment_durability_wear，证明未把进行中的战斗误结算。
