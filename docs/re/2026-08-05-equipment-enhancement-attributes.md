# 装备强化属性：基础成长与阶段附加词条

## 触发与首个偏离

强化成功后，`account_role_equipment.enhance_level` 已被持久化，客户端物品行也会显示
`+N`，但服务端 `vm_net_mock_role_collect_equipment_bonus()` 只累加 `equip.dsh` 的基础
字段，`ParseEquipAttributes(0x010185C2)` 的扩展字段始终写入 `attr-count=0`。因此首个
偏离是装备实例的强化等级没有进入权威战斗属性，也没有把每四级词条交给客户端状态重算；
不是强化界面的成功率、玄晶消耗或客户端闪退问题。

## 客户端契约

`scene_rebuild_status_meter_node(0x0100FED8)` 的顺序为：

1. 对武器的 `equip.dsh` 物攻、其他部位的护甲调用
   `CalcEquipStatBonus(0x01028B34)`；
2. 叠加 `equip.dsh` 的其余直接属性；
3. 消费 `ParseEquipAttributes` 解析出的阶段属性。

`CalcEquipStatBonus` 每级使用一项 `{ flat_u8, percent_i16 }`：

```text
bonus += flat + floor(percent * base / 100)
final = base + sum(bonus)
```

扩展属性的线上序列字段严格为：`i16 currentEnhance`、`i16 maxEnhance`、`u8 count`，
随后每项为 `u8 threshold`、`u8 type`、`u8 mode`、`i16 value`。客户端只有六个槽位，
本实现最多下发四项。已确认固定值模式为 `mode=0`；类型 `1..10` 分别覆盖力量、敏捷、
智慧、物攻、护甲、躲闪、命中、暴击、气血、法力。

## 已恢复的基础成长表

来自真实客户端计算器与下列独立样例的交叉约束：武林神胫 `1653 -> 2327 (+4)`、武林神
披 `507 -> 721 (+4)`、绒丝袍 `1122 -> 3024 (+12)`、圣诞魔杖 `374 -> 1076 (+12)`、
梦境魔杖 `529 -> 2103 (+16)`。

| 强化级 | 固定值 | 百分比 |
| --- | ---: | ---: |
| 1–4 | 2, 3, 4, 5 | 10, 10, 10, 10 |
| 5–8 | 7, 8, 9, 10 | 14, 16, 14, 16 |
| 9–12 | 14, 15, 16, 17 | 14, 16, 14, 16 |
| 13–16 | 20, 23, 26, 28 | 22, 24, 26, 28 |

这张十六档表现在由服务端用于同一件装备的权威战斗值计算；它不是 `1/29/1` 的
`data1/data2` 强化材料/费用表。

## 阶段词条设计

每个槽位预置 `+4/+8/+12/+16` 四个词条。属性方向由部位决定，例如武器偏暴击、物攻、
命中、智慧；衣甲偏法力、护甲、气血；靴子偏躲闪、命中；戒指偏物攻、气血。数值以装备
需求等级平滑缩放。样例用于确定属性方向和数值量级，而不为特定物品保留硬编码特例：

- 武林神胫：`+4 物攻 124`、`+8 护甲 525`；
- 武林魔杖：`+4 暴击 4`、`+8 物攻 124`；
- 武林神披：`+4 暴击 4`、`+8 命中 4`；
- 圣诞魔杖：`+4 暴击 4`；
- 梦境魔杖：`+4 暴击 5`；
- 名人之珠：`+4 物攻 64`、`+8 气血 300`。

因此同等级、同部位的装备总能得到一致的强化效果；装备库新增物品也不需要增加 ID 特判。

## 修改点与验证

- `src/server/mock_server_catalog.c`
  - 集中保存十六档主属性成长和部位阶段词条规则；
  - 所有背包、装备栏、商店、交易预览等复用的 common-extra 序列只下发当前强化等级
    已解锁的阶段词条：`+0..+3` 为零条，之后每达到 `+4/+8/+12/+16` 新增一条。
- `src/server/mock_server_role.c`
  - 直接基础词条后叠加强化主属性与已解锁阶段词条，作为服务端战斗权威值。
- `tmp/equipment-enhancement-attributes-regression.c`
  - 验证五个主属性样例、通用阶段词条、角色统计汇总和 common-extra 的实际类型化
    序列编码。

验证命令（不启动服务端）：

```powershell
make -j2
gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 -ffunction-sections -fdata-sections -w tmp/equipment-enhancement-attributes-regression.c src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o tmp/equipment-enhancement-attributes-regression.exe '-Wl,--gc-sections' -lpthread -liconv -lm -lkernel32 -lws2_32
.\tmp\equipment-enhancement-attributes-regression.exe
```

结果：构建成功，回归输出 `equipment enhancement attribute regression passed`。

## 未把最终基础值重复塞入扩展字段的原因

客户端原生 `CalcEquipStatBonus` 已拥有“按当前强化等级逐级成长”的职责。将服务端计算出的
最终物攻/护甲再伪装为阶段扩展词条，会在原生表可用时双重加成，也违反扩展字段“每四级
新增一条”的协议语义。因此 common-extra 只承载已解锁的阶段词条；逐级主属性由客户端
计算器和服务端权威计算器分别按同一张十六档表处理。
