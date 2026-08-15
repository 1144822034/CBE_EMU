# 装备强化基础属性与四阶段词条生效核对（2026-08-15）

## 结论

- 服务端强化等级的权威属性汇总原本只对武器攻击、非武器护甲生效。戒指等装备若这两个
  原生主属性均为 `0`，虽然强化等级成功保存，服务端战斗属性却不会获得每 `+1` 的成长。
- `+4/+8/+12/+16` 四阶段词条链路完整：装备实例创建时生成并持久化四个不重复词条，初次
  下发装备对象时携带完整计划，服务端属性汇总只在对应阈值及以后计入已经解锁的阶段。
- 服务端权威属性汇总和客户端原生规则表现已共用同一条 16 档曲线。没有向 `29/3` 增加客户端
  不识别的属性数组，也没有通过 `7/7 type=2` 或重复插入装备对象来伪造即时刷新。

## 触发条件与首个偏离

可重复的最小条件是：装备实例具有非零强化等级，但其目录数据不含客户端原生的武器攻击
或非武器护甲。例如测试戒指：`slot=7, mp=652, crit=16, armor=0, attack=0`。

完整链路如下：

1. 强化成功分支把 `enhanceLevel` 增加一，确保四阶段词条存在并持久化。
2. `29/3` 返回结果、装备序号和材料信息；客户端 parser 只更新装备对象的当前/最大强化等级。
3. 服务端构建角色属性时，从已装备实例读取 `enhanceLevel` 和 `enhanceAffixes`，调用
   `vm_net_mock_equipment_enhancement_add_bonus`。
4. 修复前该函数仅调用 `native_primary`。饰品的攻击、护甲均为零时得到 `primary=0`，于是
   强化等级存在而每级基础成长第一次丢失。

根因是回退规则 `20058ff` 正确移除了客户端属性数组中的“后备基础属性行”，因为该数组不能
被 `29/3` 原地刷新；但后备主属性选择也因此不再进入服务端权威统计。已有
`fallback_primary_type` 和 `bonus_value_for_type` 仍在目录层，却没有被属性汇总调用。

## 协议与客户端证据

- `JianghuOL.CBE:0x01028B34` 的原生基础强化计算读取控制器 `+0x584` 的 16 级规则，并只处理
  武器攻击或非武器护甲两个硬编码主字段。
- `JianghuOL.CBE:0x0101CD1E + 0x0101DD1E + 0x01028C7C` 的 `29/1..3` 路径表明：`29/3`
  只更新装备对象 `+286/+287`，不重建属性数组。
- `ParseEquipAttributes(0x010185C2)` 在装备对象创建时读取 `{threshold,type,mode,value}` 行。
  因此未来阶段必须在 `30/21` 或 `7/7 type=1` 创建实例时完整下发，而不能在强化成功后用
  不属于协议契约的包补写。

## 修复

`src/server/mock_server_catalog.c` 新增统一的主属性解析：

- 武器且攻击非零：以攻击为基础属性；
- 其他装备且护甲非零：以护甲为基础属性；
- 两者均不存在：从装备自身的非零基础属性中选出既有后备主属性，并把每级累计强化值加到
  该真实字段；不再错误地统一加到护甲。

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
equipment enhancement regression passed: title login initializes the native 16-level rule table; every +1 grows the resolved base attribute; +4/+8/+12/+16 stages activate exactly
```

覆盖内容：

- 基础值 `100` 的武器攻击和防具护甲从 `+0` 到 `+16` 的累计增量依次为
  `0,12,25,39,54,75,99,122,148,176,207,237,270,312,359,411,467`，每次 `+1`
  都严格增加；
- 无原生攻防的测试戒指以 `MP=652` 为后备基础属性，`+1` 增加 `67`，`+2` 累计增加 `135`，
  且不会误加护甲、攻击或暴击；
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
