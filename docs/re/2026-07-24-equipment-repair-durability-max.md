# 装备修理耐久上限

## 状态

已修复并以隔离服务回归验证。

## 触发与原始症状

角色穿戴木制宽剑后，经修理 NPC 修理，客户端装备界面出现当前耐久大于该装备
显示最大耐久的状态。

已保留的数据库现场显示了首次偏离：

```text
role_id=10023 slot=0 item_id=1001 durability=82 durability_max=100
```

## 客户端、资源和服务端链路

1. `mmGameMstarWqvga.cbm:sub_D04(0x00000D04)` 消费 `1/7/7 type=2` 的
   `iteminfo` 行，读取 `seq, itemId, currentCount`；`currentCount` 写入
   装备的当前耐久字段。
2. 客户端的物品显示元数据来自本地 `equip.dsh`。该表 26 列，零基第 19 列名为
   `耐久`：1001 木制宽剑为 50、1101 桃木宽剑为 80，表内 1485 行均有正数
   耐久值，范围 20 到 999。
3. 修理服务入口是已验证的 `26/1 {type=2,id=0xe3000001}`。它调用
   `vm_net_mock_role_service_repair_cost`，再调用
   `vm_net_mock_role_service_repair_all`，并将结果写入
   `account_role_equipment_durability`。
4. 旧实现没有解析 `equip.dsh` 的第 19 列，给新装备、内存默认状态和修理一律
   使用伪造的 100 上限。因此修理会把 1001 的 `currentCount` 写为 100，而
   客户端仍按本地资源显示 50；崩溃或 UI 刷新问题都不是这一偏离的根因。

## 根因

违反的契约是“服务端下发的装备当前耐久不得超过客户端同一 `itemId` 的
`equip.dsh.耐久`”。首次错误状态发生在服务端建立耐久记录时，而不是修理确认
或客户端渲染时：`durability_max=100` 被持久化给实际最大值为 50 的 1001。

## 修复

- 装备目录现在解析并保存 `equip.dsh` 第 19 列。
- 装备变更时以该值创建满耐久；修理费用和修理结果使用同一值。
- 读取旧记录后，在计算报价或执行修理前，以当前已穿戴 `itemId` 的 DSH 值校正
  `durability_max`；若旧 `durability` 已高于真实上限则一并截断，再持久化。
- 无法解析到非零装备的 DSH 记录时只记录 `unresolved`，不写入猜测上限。
- 未给修理的 `26/1` 对话响应追加未经证明的装备替换对象；当前值仍沿用已验证的
  `1/7/7 type=2` 装备 bootstrap 通道。

## 回归

`scripts/equipment-durability-max-regression.php` 建立两条旧格式记录：

```text
1001: 44/100  -> 登录 bootstrap 后 44/50  -> 修理后 50/50
1101: 70/100  -> 登录 bootstrap 后 70/80  -> 修理后 80/80
```

隔离服务结果：

```text
equipment durability max regression passed repaired=50/50,80/80 money=984
```

并已执行 `make -j2`。
