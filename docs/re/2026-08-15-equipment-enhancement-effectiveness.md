# 装备强化基础属性与四阶段词条生效核对（2026-08-15）

## 结论

- 每级基础强化只有两种固件主属性：类别 `7/8/9` 增加物攻，类别 `0..6` 增加护甲。
  戒指等装备若基础护甲为 `0`，强化等级仍可保存，但不会推断法力、生命等替代成长。
- `+4/+8/+12/+16` 四阶段词条链路完整：装备实例创建时生成并持久化四个不重复词条，初次
  下发装备对象时携带完整计划，服务端属性汇总只在对应阈值及以后计入已经解锁的阶段。
- 服务端权威属性汇总和客户端原生规则表现已共用同一条 16 档曲线。没有向 `29/3` 增加客户端
  不识别的属性数组，也没有通过 `7/7 type=2` 或重复插入装备对象来伪造即时刷新。

## 触发条件与首个偏离

可重复的最小偏离是：服务端根据目录数值选择非固件属性。例如法杖同时具有物攻和更高的
智慧时选择智慧，或测试戒指具有 `mp=652, crit=16, armor=0` 时选择法力。

完整链路如下：

1. 强化成功分支把 `enhanceLevel` 增加一，确保四阶段词条存在并持久化。
2. `29/3` 返回结果、装备序号和材料信息；客户端 parser 只更新装备对象的当前/最大强化等级。
3. 服务端构建角色属性时，从已装备实例读取 `enhanceLevel` 和 `enhanceAffixes`，调用
   `vm_net_mock_equipment_enhancement_add_bonus`。
4. 先前服务端在这里增加了按目录候选取最大值以及缺失字段回退，第一次把法杖物攻改成
   智慧、把无护甲饰品改成法力或生命；客户端仍按物攻/护甲显示。

根因是把 `equip.dsh` 的基础属性分布误当成了强化属性选择规则。固件的分类分支和标题
`1/1/4` 规则表都没有这种自适配契约，因此服务端推断造成了显示、角色统计和战斗结算分歧。

## 协议与客户端证据

- `JianghuOL.CBE:0x01028B34` 的原生基础强化计算读取控制器 `+0x584` 的 16 级规则，并只处理
  武器攻击或非武器护甲两个硬编码主字段。
- `JianghuOL.CBE:0x0101CD1E + 0x0101DD1E + 0x01028C7C` 的 `29/1..3` 路径表明：`29/3`
  只更新装备对象 `+286/+287`，不重建属性数组。
- `ParseEquipAttributes(0x010185C2)` 在装备对象创建时读取 `{threshold,type,mode,value}` 行。
  因此未来阶段必须在 `30/21` 或 `7/7 type=1` 创建实例时完整下发，而不能在强化成功后用
  不属于协议契约的包补写。

## 修复

`src/server/mock_server_catalog.c` 的主属性解析改为严格匹配固件类别：

- `category=7..9` 固定以物攻为基础属性；
- `category=0..6` 固定以护甲为基础属性；
- 对应基础值为零或类别未知时不产生每级基础增量，不选择其他字段。

主属性和阶段词条改为共用按属性类型累加的窄 helper，避免两套 switch 出现字段差异。
`src/server/mock_server_equipment_npc.c` 在每次强化成功后增加审计行：

```text
mock_equipment_enhance_effect ... level_before=... level_after=...
primary_type=... primary_base=... primary_bonus_before=...
primary_bonus_after=... primary_step=...
stage_threshold=... stage_type=... stage_value=...
```

非四级阈值的 `stage_threshold/type/value` 为零；跨越 `+4/+8/+12/+16` 时记录本次首次解锁
的词条。日志只读取已经完成的状态，不改变响应对象或客户端状态。

## 四阶段核对结果

| 强化等级 | 服务端已生效词条 | 初次装备对象中的计划 |
| --- | --- | --- |
| `+0..+3` | 无 | 已携带 `+4/+8/+12/+16` 四行 |
| `+4..+7` | `+4` 词条 | 四行保持不变 |
| `+8..+11` | `+4、+8` 词条 | 四行保持不变 |
| `+12..+15` | `+4、+8、+12` 词条 | 四行保持不变 |
| `+16` | 四个词条全部生效 | 四行保持不变 |

回归还断言：词条类型不重复、四个值均非零；关系字段打包/解包不改变计划；后续强化再次调用
`ensure_affixes` 不重掷已经有效的实例词条。

## 验证

构建：

```powershell
make -j2
```

结果：通过，客户端目标无须重编译，服务端目标成功重编译并链接。

隔离回归：

```powershell
gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 `
  -ffunction-sections -fdata-sections -w `
  scripts/equipment-enhancement-fallback-regression.c `
  obj/server/gifDecode.o obj/server/mystd.o obj/server/mysql-client.o `
  obj/server/md5.o -Wl,--gc-sections `
  -o tmp/equipment-enhancement-fallback-regression.exe `
  -lpthread -liconv -lm -lkernel32 -lws2_32
.\tmp\equipment-enhancement-fallback-regression.exe
```

结果：

```text
equipment enhancement regression passed: title login initializes the native 16-level rule table; only weapon attack and non-weapon armour grow at every +1; +4/+8/+12/+16 stages activate exactly
```

覆盖内容：

- 基础值 `100` 的剑、匕首、法杖物攻和防具护甲从 `+0` 到 `+16` 的累计增量依次为
  `0,12,25,39,54,75,99,122,148,176,207,237,270,312,359,411,467`，每次 `+1`
  都严格增加；
- 法杖即使智慧高于物攻也只增加物攻；披风即使生命/法力高于护甲也只增加护甲；
- 基础护甲为零的腰带、戒指以及仅有生命的披风不会回退到生命、法力或暴击；
- 固定四阶段计划逐级遍历 `+0..+16`，只在 `+4/+8/+12/+16` 精确增加一个已生效词条；
- 初次 wire 属性在 `+0` 已包含全部四个未来阶段，持久化往返和后续强化不改变计划。

本轮没有运行需要数据库写入的完整客户端场景：当前环境没有提供隔离测试数据库凭据，按仓库
隔离规范不能连接或修改用户正在使用的 `jh_online` 数据。服务端纯函数、持久化表示和协议
边界已由无数据库回归覆盖；后续具备隔离夹具时，可用新增审计行复测真实角色强化前后的战斗
属性与客户端显示。

## 客户端原生规则表初始化已补全

继续追踪动态模块后，确认写入器位于
`mmTitleMstarWqvga.cbm:title_parse_equipment_enhance_primary_rules(0x1568)`。它由角色列表
`WT 1/1/4` parser 调用，从同一对象的 `num` 与 `data` 字段分配并填写
`itemCtrl+0x584`。每行正是客户端 `CalcEquipStatBonus` 消费的 `{flat:u8,pad,percent:i16}`。

根因是两条服务端 subtype-4 builder 均遗漏这两个字段。现在它们下发 `num=16` 和 112 字节
tagged rule stream，并直接复用服务端权威统计使用的同一曲线。这样登录模块通过原生 parser
完成初始化，背包详情可以在已有强化等级上逐级累计基础物攻/护甲；`29/1.data1/data2` 仍只
表示材料和玄晶能力，没有被误作规则表。

完整写入点、调用链、首个偏离和协议字节见
`docs/re/2026-08-14-equipment-enhancement-primary-rule-init.md` 第 11 节。当前剩余边界只有隔离
账号下的完整登录与背包画面验收；代码与无数据库协议回归已经通过。

## 主属性与固件一致

服务端强化主属性严格复用客户端固件分支：装备类别 `7/8/9`（剑、匕首、法杖）只强化
物攻，类别 `0..6` 只强化护甲。不会按 `equip.dsh` 中数值最大的字段改成力量、敏捷、
智慧、生命或法力，也不会在目标基础值为零时回退到其他属性。

标题规则表只携带 16 档 `{flat,percent}`，没有属性类型字段，因此此规则同时保证背包显示、
服务端角色统计和战斗结算采用同一口径。固件分支、修正原因和回归样本详见
`docs/re/2026-08-15-equipment-enhancement-native-primary-rule.md`。
