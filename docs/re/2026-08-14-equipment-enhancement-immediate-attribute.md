# 装备强化：零物攻/零护甲饰品的即时属性成长

## 触发与首个偏离

角色 `10036` 对背包实例 `seq=70,item=40088`（`equip.dsh`：梦境布饰，类别 6，
法力 `652`、暴击 `16`、物攻 `0`、护甲 `0`）连续完成了两次强化。运行日志已确认：

```text
phase=3 seq=70 item=40088 level=0 result=1 success=1
phase=3 seq=70 item=40088 level=1 result=1 success=1
```

但物品属性没有变化。持久化和 `1/29/3` 成功判定均正确；最早错误发生在服务端的
`vm_net_mock_equipment_enhancement_primary_bonus()`：它把所有非武器都以 `bonus.armor`
作为成长基数。梦境布饰的护甲为零，因此 `+1`、`+2` 的成长结果始终为零。

这不是“未穿戴装备不应加角色属性”的问题。背包内装备仍不得计入角色属性；这里只修复
装备实例自身的属性描述及其在**穿戴后**进入角色/战斗属性汇总的数据。

## 客户端证据与协议边界

| binary | 地址 | 已确认行为 |
| --- | --- | --- |
| `江湖OL.CBE` | `HandleItemUseAndEquip(0x01028C7C)` | `1/29/3` 成功只读取 `tnum/equipseq/occult`，并调用 `UpdateTaskProgressEntry(1, ...)` 更新匹配实例的 `item+286` 强化等级。该包不读取装备属性条目。 |
| `江湖OL.CBE` | `UpdateTaskProgressEntry(0x01028726)` | 成功时按 `seq` 写入当前/最高强化等级；不会重建扩展属性数组。 |
| `江湖OL.CBE` | `ParseEquipAttributes(0x010185C2)` | 标准装备详情读取 `i16 current`、`i16 max`、`u8 count`，随后逐条读取 `threshold/type/mode/i16 value`。 |
| `江湖OL.CBE` | `scene_rebuild_status_meter_node(0x0100FED8)` | 仅对已穿戴实例应用属性：武器原生计算物攻，非武器原生计算护甲，然后读取扩展属性。 |
| `江湖OL.CBE` | `AccumEquipStatBonus(0x0100FE9E)` | 当 `currentEnhance >= threshold` 时，把固定或百分比扩展条目加入相应属性。 |
| `mmGameMstarWqvga.cbm` | `sub_418C(0x418C)` | `17/1.iteminfo` 读取实例的强化等级；随后通过 `equip.dsh` 载入该物品的静态描述/阶段词条。它不是武器基础物攻的实例覆写通道。 |
| `江湖OL.CBE` | `CalcEquipStatBonus(0x01028B34)` | 将每档强化规则解释为 `u8 fixed + floor(i16 percent * base / 100)`；武器的基础物攻与当前强化等级通过这条路径重算。 |
| `江湖OL.CBE` | `HandleItemUseAndEquip(0x01028C7C)` 的 `29/1` 分支 | 从 `data1/data2` 读取逐档规则，调用 `CalcEquipStatBonus` 后重建强化界面的属性文本。 |

结论：`29/3` 必须保持其确证字段顺序。它只提交强化等级和材料消耗；`17/1` 只能刷新
背包实例等级，不能覆写桃木宽剑在 `equip.dsh` 中的基础物攻。逐级物攻必须由对应强化流程
的 `29/1.data1/data2` 以客户端原生规则格式下发。

## 修复设计

1. 保留原生规则：武器以物攻、护甲类装备以护甲走客户端的逐级 `CalcEquipStatBonus`。已装备的
   `7/7` 列表不重复发送这些主属性，避免双重加成。
2. 当原生主属性为零时，从该装备的非零基础属性中选取一个稳定的成长主属性：
   物攻、护甲、生命、法力、力量、敏捷、智慧、躲闪、命中、暴击（依该顺序）。抗性没有
   已确认的扩展条目类型，不作为回退目标。
3. 按已恢复的十六档强化表计算该属性在当前等级的累计值，并用已确认的
   `threshold=1,type=<属性>,mode=0,value=<累计值>` 编入 common-extra。现有的
   `+4/+8/+12/+16` 随机阶段词条继续保留在其后。
4. 服务端角色属性只在 `vm_net_mock_role_collect_equipment_bonus()` 枚举已穿戴、耐久有效的
   实例时叠加同一个回退成长；背包物品不参与角色属性。
5. `1/29/3` 成功后追加标准 `17/1` 全背包详情，以便客户端更新实例强化等级；失败、材料不足
   与上限分支绝不追加刷新对象。武器/护甲的主属性增量不再伪造为 common-extra，而由下一次
   `29/1` 的客户端原生重算呈现。

## 回归边界

- 梦境布饰 `MP=652`：`+1` 应出现法力增益，`+2` 大于 `+1`；未穿戴时角色总属性不变。
- 木制宽剑等有物攻的武器、护甲非零的防具仍只走原生主属性计算，不产生重复的回退条目。
- 穿戴已强化回退饰品后，服务端战斗汇总和客户端场景状态重建都应用同一增益。
- `1/29/3` 失败响应仍只有原始 `29/3` 对象；成功响应为有序的 `29/3 + 17/1` 两对象 WT 包，
  总对象数不超过客户端 `event_packet_parse_WT(0x0103467A)` 的十对象上限。

## 2026-08-14：桃木宽剑 +1 的可见物攻

### 现象与根因

`item=1101` 的桃木宽剑基础属性为“力量 +9、物攻 +34”。强化成功时服务端已经正确保存了
`enhance_level=1`，并在 `29/3 + 17/1` 中发送了该等级；但背包详情仍只显示资源文件中的
基础物攻 `34`，看起来像强化未生效。

客户端并不允许服务端为单个装备实例覆写 `equip.dsh` 的基础攻击字段。`mmGame:sub_418C`
在读取 `17/1.iteminfo` 后会从 `equip.dsh` 重装静态属性；先前尝试塞入 common-extra 的主属性
条目因此不会进入“物攻 +34”这行文本。已穿戴武器又会按强化等级原生计算物攻，重复发送同一
增量还会引发双重加成风险。

`力量 +9` 是装备原始固有属性，CBE 的原生强化计算不会在 +1 改写它；这个值保持不变是正确的。

### 修正

- `29/1.data1/data2` 改为客户端真实的 4 字节强化规则：低位字节为固定值、位 `16..31` 为
  百分比。它们与服务端角色/战斗属性使用同一份十六档强化曲线。
- 桃木宽剑基础物攻 `34`：+1 按 `{fixed=2, percent=10}` 得到 `39`；+2 再叠加
  `{fixed=3, percent=10}` 得到 `45`。力量 `+9` 是该剑的固有附加属性，不由这条武器物攻
  强化规则改变。
- 删除无效的“背包详情主属性 common-extra”注入；已装备和未装备都只保留客户端原生的一次
  主属性计算，避免双重加成。

### 回归

- `scripts/equipment-enhancement-fallback-regression.c` 断言客户端规则编码的第一、二档分别为
  `{2,10%}`、`{3,10%}`，并断言以基础物攻 `34` 算出的 +2 累计增量与服务端计算相同。

验证命令（不启动服务端）：

```powershell
gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 -ffunction-sections -fdata-sections -w scripts/equipment-enhancement-fallback-regression.c obj/server/gifDecode.o obj/server/mystd.o obj/server/mysql-client.o obj/server/md5.o -Wl,--gc-sections -o tmp/equipment-enhancement-fallback-regression.exe -lpthread -liconv -lm -lkernel32 -lws2_32
.\tmp\equipment-enhancement-fallback-regression.exe
```
