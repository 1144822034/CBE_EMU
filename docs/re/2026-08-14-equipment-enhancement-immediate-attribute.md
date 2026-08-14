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

### 已撤销的错误假设

此前把 `29/1.data1/data2` 当作客户端主属性规则，并将其编码为 `{fixed,percent}`。重新核对
`HandleItemUseAndEquip(0x01028C7C)` 后可知，这两张表由强化界面用于材料需求和玄晶档位；
`CalcEquipStatBonus(0x01028B34)` 读取的是另一处已初始化的控制器表。因此该编码既不能让
背包详情的物攻/护甲重算，也会破坏强化材料界面的原生语义。

已恢复的 `29/1` 契约为：`data1[0..16]=(level+1)*100` 的需求表、
`data2[0..15]=tier*100` 的玄晶能力表。服务端战斗属性仍使用独立的十六档平衡曲线；它不是
客户端详情页主属性规则表的替代品。

### 回归

- `scripts/equipment-enhancement-fallback-regression.c` 断言服务端战斗汇总的防具基础护甲 `45`
  在 +2 时累计增量为 `13`；它不再把这一服务端曲线误称为 `29/1` 客户端规则。

验证命令（不启动服务端）：

```powershell
gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 -ffunction-sections -fdata-sections -w scripts/equipment-enhancement-fallback-regression.c obj/server/gifDecode.o obj/server/mystd.o obj/server/mysql-client.o obj/server/md5.o -Wl,--gc-sections -o tmp/equipment-enhancement-fallback-regression.exe -lpthread -liconv -lm -lkernel32 -lws2_32
.\tmp\equipment-enhancement-fallback-regression.exe
```

## 2026-08-14：+4 阶段词条的实例初始化时机

### 最新运行时证据

角色 `10036` 的桃木宽剑实例 `seq=282,item=1101` 在连续强化中已经成功跨过
`+4`：服务端日志显示 `phase=3 level=3 result=1 success=1`，随后完整背包详情的
`iteminfo_len` 从 `972` 增至 `985`。这证明服务端确实在该时点生成、持久化并序列化了一条
阶段词条；“没有词条”不是随机值、数据库保存或字段长度不足造成的。

### 客户端契约与首次偏离

| binary | 地址 | 已确认行为 |
| --- | --- | --- |
| `mmGameMstarWqvga.cbm` | `sub_D04(0x00000D04)` | `1/7/7` 逐行调用 `ParseEquipAttributes`，把词条数量和四组数组交给 `ApplyEquipVisualData`，随后 `type=1` 交给物品管理器。该路径是物品实例第一次进入管理器时的完整属性初始化。 |
| `江湖OL.CBE` | `UpdateTaskProgressEntry(0x01028726)` | `1/29/3` 成功后只按背包序号写 `item+286` 当前强化等级与 `item+287` 上限；不会创建或替换阶段属性数组。 |
| `江湖OL.CBE` | `HandleItemUseAndEquip(0x01028C7C)` | `29/3.occult` 仅用于消耗强化材料；不存在强化成功时接收一份新词条数组的字段。 |
| `mmGameMstarWqvga.cbm` | `sub_418C(0x0000418C)` | `17/1.iteminfo` 的词条数量保存到局部变量，未写回背包实例的属性计数；它不能补救已经创建的实例。 |
| `江湖OL.CBE` | `TimerControl_ProcessItem(0x01032EB8)` | `7/7 type=1` 对装备类（客户端内部类别 15）按新实例插入而不是按序号覆写；因此不能在强化成功后用它“补发一件同序号装备”。 |

因此原来的首次偏离是：服务端直到实际到达 `+4` 才为实例生成、下发第一条随机词条。
此时客户端已有的背包装备对象已经建立，`29/3` 又只会改变强化等级；随后追加的
`17/1` 不能把该词条数组装入旧对象，所以 UI 既不显示词条，也无法由该对象将词条用于
属性汇总。

### 已实施修复

1. 装备实例取得唯一背包序号后立刻确定并持久化四条 `+4/+8/+12/+16` 随机词条；低强化
   等级不再清空尚未解锁的未来词条。
2. `1/7/7 type=1` 的首次物品实例行写入完整四条计划词条（门槛保留为 4/8/12/16）。客户端
   已有对象只会在强化成功的 `29/3` 中增加等级，因此会在 +4 自然显示第一条、在后续阶段
   继续解锁，而不需要伪造额外刷新包。
3. 删除 `29/3 + 17/1` 的补包：它不是强化成功契约，并且已经被客户端实测为不能更新旧实例
   的词条数组。

实现位置：

- `mock_server_role.c:vm_net_mock_role_append_backpack_equipment_instance()`：序号分配后按
  `roleId ^ itemId ^ seq` 生成并保存四档随机词条；穿脱返回背包也保留同一实例词条。
- `mock_server_catalog.c:vm_net_mock_equipment_enhancement_collect_wire_attrs()`：首个物品实例
  行同时编码已生效条目与未来三档/四档阶段条目，避免把未来门槛误当作“不应下发”。
- `mock_server_catalog.c:vm_net_mock_build_item_use_iteminfo_blob()`：`7/7 type=1` 以
  `itemId + seq` 回查刚保存的装备实例，使用该实例的强化等级和词条，而非旧的零扩展块。
- `mock_server_equipment_npc.c`：成功的 `29/3` 回归为单一原生强化结果对象，不再追加无效的
  `17/1` 全背包包。

`make -j2` 已通过；本轮没有启动服务端或改写客户端状态。

已有的旧实例在本次修复前已进入客户端物品管理器，不能用 `7/7 type=1` 原地覆盖，否则会
重复插入装备。它们会在下一次**正常重新取得实例**时带上完整计划；已穿戴的旧实例则可在
正常重新登录时通过登录装备初始化包获得完整属性。不得用“删除当前选中物品再重新插入”的
`7/7 type=2 + type=1` 组合冒充刷新，因为 `type=2` 没有目标序号，客户端只会删除当前
选中项，违反物品实例契约。

## 2026-08-14：强化等级更新、装备主属性未重算

Status: investigating

### 现象与可重复条件

装备 `3001`（生铁硬胸）基础护甲为 `45`。客户端详情页已经显示“强化：2”，但同一页仍显示
“护甲：+45”，没有随等级重算。按当前服务端已在强化页显示的十六档规则，`+2` 的期望结果应为
`45 + (2 + floor(45 * 10 / 100)) + (3 + floor(45 * 10 / 100)) = 58`。

因此问题不在强化请求、实例持久化或强化等级字段；首个可见偏离是客户端的主属性计算没有取得
它所依赖的规则表。

### 已确认的客户端链路

1. `1/29/3` 的 `HandleItemUseAndEquip(0x01028C7C)` 调用
   `UpdateTaskProgressEntry(1, ...)`，只把成功实例的当前/最高强化等级写入 `item+286/+287`。
2. 装备详情 `BuildItemTooltipString(0x01032188)` 读取 `item+286`，对防具把 `item+248`
   作为基础护甲传入 `CalcEquipStatBonus(0x01028B34)`，然后渲染其返回值。
3. `CalcEquipStatBonus` 将全局场景对象 `Global_R9+0x54AC` 的 `+0x580` 作为强化状态块，
   从其中 `+4`（即场景对象 `+0x584`）取得十六项 `{u8 fixed, i16 percent}` 规则表；每一级
   累加 `fixed + floor(percent * base / 100)`。
4. `29/1.data1/data2` 的解析目标是强化界面状态块中的 `+0x5D0/+0x5CC`，用于材料与预览；
   它们不是 `CalcEquipStatBonus` 读取的 `+0x584` 规则表。之前把二者视为同一来源的结论不成立。

### 隔离运行时证据

隔离场景 `equipment-enhance-rules-probe-v1` 在独立端口 `19190/19191`、临时 MySQL schema 和
复制的客户端资源中运行。它只通过模拟器原有触摸事件队列进入“场景 → 装备 → 已穿戴武器详情”，
并在 `CalcEquipStatBonus` 的真实 PC 命中时只读记录状态：

```text
equipment_enhance_rules_entry pc=01028b5c global_r9=01050bd0
equipment_enhance_rules_pointer_probe ctrl_read=0 ctrl=0500b210 table_read=0 table=00000000
```

该测试尚未进入背包的“强化”确认页，所以它不是对 `29/1` 初始化时机的最终否定；但已证明在普通
详情重算发生时表指针确为零，且当前服务端登录/装备列表/详情流程没有提供该表。

### 已排除的修复方式

- 仅重发 `29/3`：该原生响应没有属性数组字段，也只更新强化等级。
- 在成功后补发 `17/1`：`mmGame:sub_418C` 不会把已存在实例的扩展数组回写为详情主属性，且它
  不能初始化场景对象的规则表。
- 用 `7/7 type=1` 重新塞同序号装备：客户端把装备视为新实例插入，破坏实例唯一性。
- 把主护甲增量伪造成 common-extra：会额外显示一条属性，不能让原生“护甲”行由 `45` 变为
  `58`，并可能和角色总属性汇总重复叠加。

### 待确认与下一步

当前未知的是客户端原生在何时、通过哪个响应对象或资源初始化场景对象 `+0x584`。

- 下一步自动化必须使用一件背包装备，真实进入 `1/29/1` 的强化界面，并在请求前、响应后和
  详情重绘时记录该指针及其写入来源。
- 在确认该初始化契约之前，不得改写客户端内存、不得把服务端计算出的护甲覆盖到静态
  `equip.dsh` 字段，也不得继续扩展 `29/1.data1/data2` 作为猜测性替代。
