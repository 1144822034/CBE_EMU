# 战斗结算后的 HP/MP 状态权威性（2026-08-09）

## 触发与首个偏离

复现条件是自动挂机的一场战斗在非满 HP/MP 时结束，并在同一次结算中触发神仙壶/逍遥壶
储量恢复或 `CBE_BATTLE_RECOVER_HP` / `CBE_BATTLE_RECOVER_MP` 配置恢复。服务端角色与下一场
战斗已经是恢复后的数值，场景顶部 HP/MP 却会回退到施放技能后、恢复前的数值。

首个偏离在终局动作响应的 `1/4/6.teaminfo`，而不是场景 UI：旧实现先在技能扣蓝后序列化
`teaminfo`，再附加 `1/4/7` 恢复增量。例如，MP 从 `925` 扣为 `890`，随后结算恢复 `+35`；
同一响应的 `4/6` 仍是 `890`，`4/7` 则是 `+35`。客户端离开结算面板后恢复的是 `4/6` 缓存，
所以顶部 MP 错为 `890`，尽管持久化值与下次开战均为 `925`。

## 客户端契约

按 `binary_name=mmBattleMstarWqvga.cbm` 选择 IDA 实例的证据：

- `InitActionSlot_B(0x6DBC)` 读取 `4/6.teaminfo` 的三组重叠数值，但只把第三个值（MP）写入
  战斗角色缓存；它不是场景 HP 的直接写入通道。
- `HandleBattleSettleMsg(0x743C)` 读取 `1/4/7.hp`、`mp`；
  `BattleSettle_UpdateCharAttrs(0x2C50)` 对主 CBE 角色的 HP/MP **累加**这些字段后按最大值截断。
  因此 `4/7.hp/mp` 是增量，不能伪装为最终绝对值。

这说明同一终局响应里 `4/6.teaminfo` 的 MP 必须已经表示 `4/7` 生效后的最终绝对值；HP/MP 的
场景写入仍由 `4/7` 的增量完成。包顺序不能倒置，也不能让旧 MP 缓存和增量结算相互矛盾。

## 修复

- `vm_net_mock_battle_project_terminal_vitals` 用活动角色的私有副本投影本次终局：先按现有道具
  规则投影壶的恢复，再投影已配置的战斗恢复；它不消耗真实物品、不修改真实角色，也不改变
  后续由 `4/7` 所拥有的结算时序。
- 只有终局内联结算的 `4/6.teaminfo` 使用该投影值；普通技能动作仍保留实际扣蓝后的当前值。
- 既有 `4/7` 路径继续以实际恢复增量更新角色、队伍快照、会话 HSP 与数据库，保证服务端权威
  值和客户端缓存值在同一个终局契约上收敛。

对应服务端日志为：

```text
mock_battle_terminal_action_vitals ... hp=95/295 mp=80/205 source=4/6-teaminfo-before-4/7
mock_battle_settle ... vitals=95/295,80/205 recover=15/10 ...
```

## 隔离回归

`hangup-auto-vitals-recovery-v1` 使用唯一的
`jh_online_autotest_<hex>` 数据库、19190/19191 端口和独立客户端/资源目录。夹具将角色设为
HP/MP `80/70`，服务端配置 `+15/+10` 恢复。场景由正常硬件输入进入挂机；看到原生 `4/7`
面板后，测试辅助只通过模拟器输入队列发送一次按下/释放，客户端自行发出 `25/5` 退出。

成功条件同时要求：

- `4/6.teaminfo` 投影为 `95/295,80/205`（其中客户端消费的是最终 MP `80`）；
- 同一次 `4/7` 结算记录最终值和 `15/10` 增量；
- 客户端回到场景后，仅读取的场景角色节点为 `HP 95/295、MP 80/205`。

测试不写客户内存、不修改 CBE/CBM 指令、寄存器、PC/LR 或网络包。运行：

```powershell
$env:CBE_AUTOMATION_MYSQL_PASSWORD='***'
powershell -NoProfile -ExecutionPolicy Bypass `
  -File scripts/run-shop-return-hangup-automation.ps1 `
  -Scenario hangup-auto-vitals-recovery-v1
```

## 实际账号复测与继续取证（2026-08-09）

上述 `4/6.teaminfo` 投影确实修复了“终局动作缓存仍为扣蓝后值”的一类问题，但不能再把它当成
所有顶部 HP/MP 偏差的泛化解释。账号 `21642502`、角色 `10036` 的最近一轮真实服务端证据已经
是：

```text
mock_battle_terminal_action_vitals role=10036 hp=615/615 mp=925/925
mock_battle_settle ... vitals=615/615,925/925 recover=0/35 auto_recover=0/35
mock_battle_operate ... teaminfo=10036:615/925 ... mp=925/890
mock_hangup_battle_start ... rolehp=615/615 rolemp=925/925
```

也就是说，该响应在线路上已把 `4/6` 的最终 MP 设为 925，并以 `4/7.mp=+35` 完成正常恢复；
下一场服务端战斗也从 925 开始。若顶部条仍不同步，首次偏离只能在客户端对这组已正确下发的
数值的结算/返回场景后的写入链，或其后的另一个写入者，不能继续通过改服务端数值猜测修复。

为定位该偏离，客户端增加了只读 `mock_hangup_vital_trace`：它从已校验的 `4/7` 读取恢复增量，
在同一个客户回调前后，以及首次执行 `scene_draw_status_panels(0x0101466A)` 时读取场景节点
`+0xB4/+0xB8/+0xBC/+0xC0`。示例隔离运行已得到：

```text
queued-before-4-7 ... hp=80/295 mp=70/205 delta=15/10
callback-after-4-7 ... hp=80/295 mp=70/205 delta=15/10
scene-hud-after-4-7 ... hp=95/295 mp=80/205 delta=15/10
```

日志保存在玩家独立目录的 `logs/hangup-protocol.log`。这项探针不改包、不写客户内存，也不更改
事件顺序；下一次真实复现将用它确定究竟在哪个阶段发生分叉，然后只修复该状态所有者。

补充的 CBE 指令级证据表明，`scene_draw_status_panels(0x0101466A)` 并不以场景节点
`+0xBC/+0xC0` 作为进度条分母：它从 `R9+0x5CA4` 取当前节点的 `+0xB4/+0xB8`，再从
`R9+0x5CAC` 所指的状态计量对象读取 `+0xC4/+0xC8` 作为 HP/MP 显示上限。探针现会同时记录
这两个指针和 `bar_max`。因此下一轮可明确区分“数值本身错误”和“数值正确但 HUD 分母缓存滞后”，
而不是继续把两类问题混为一谈。

实际账号 `10036` 的第二轮证据已经排除了“结算增量未到达 HUD”的假设：终局后场景节点仍为
`615/615,925/925`，但 HUD 计量对象为 `bar_max=2966/3193`。该角色等级为 34，而持久化的八个
`account_role_equipment` 实例（40067、40070、40073、40076、40078、40082、40085、40088）在
`equip.dsh` 中的 `levelRequired` 全为 70，且耐久均为正。

服务端在正常“穿戴/替换装备”请求中已经拒绝 `levelRequired > role->level`；因此战斗最大值约
`615/925` 是正确规则，而不是需要向客户端靠拢的缺失计算。客户端
`scene_rebuild_status_meter_node(0x0100FED8)` 只看耐久，不重做等级校验，所以它忠实地把这个
**不应存在的持久化已穿戴状态**计入 HUD。首个错误状态是数据库装备栏，不是终局 `4/6`、`4/7`
或场景同步包。

修复位于角色持久化加载边界：`vm_net_mock_role_recover_overlevel_equipment` 以角色 EXP 推导的
实际等级检查每个已穿戴实例，将超过等级的、可识别且槽位匹配的装备完整迁回背包（保留物品、
耐久和强化等级），然后由既有 relational writer 在一个 MySQL 事务中同时替换装备栏和背包行。
背包空间不足时采取全有或全无：不丢弃、不部分迁移，保留原始状态并记录
`equipment_level_state_repair_blocked` 供管理员明确处理。此时
`vm_net_mock_role_equipment_slot_is_usable` 仍把该行判为**不可穿戴**；装备登录 `7/7 type=2`、
装备服务缓存、耐久结算和附近玩家装备摘要都会隔离该行。这个窄范围的读取隔离不是伪造成功：
它复用正常穿戴端点的同一等级规则，防止一个客户端不校验等级的解析器把异常数据库行重新变成
HUD 属性；原始实例保持完整，待有足够空间后下次加载再走事务迁回。

账号 `21642502`、角色 `10036` 当前另有一个独立的背包契约异常：关系表为 `99/100`，而
JianghuOL.CBE 的主物品管理器只公开 64 个逻辑格。因此八个需迁回的装备不能安全地塞入客户端
可见背包。本轮修复会先隔离其“已穿戴”投递，使 HP/MP HUD 立即与战斗一致；不会把装备写入
第 65 格以后这种客户端不可见位置。清理背包空间后重新加载即可完成迁回，且日志会从
`equipment_level_state_repair_blocked` 转为 `equipment_level_state_repair ... moved=8`。

## 客户端战斗阶段是否重验装备等级

结论：**不会**。按 `binary_name=江湖OL.CBE` 选择 IDA 实例的
`scene_rebuild_status_meter_node(0x0100FED8)`，客户端场景状态表重建对每一条已装备记录只检查
`currentDurability > 0`，随后按类别和强化属性累加；其条件中没有读取角色等级，也没有读取
`equip.dsh` 的 `levelRequired`。因此一旦 `7/7 type=2` 把高等级装备装入客户端装备链，HUD 会
无条件把它计入。

按 `binary_name=mmBattleMstarWqvga.cbm` 选择 IDA 实例的
`HandleBattleStartMsg(0x66CC)` 也不读取装备链：场景战斗（type 5）直接从 CBE 的既有场景角色节点
复制角色 ID、当前/最大 HP/MP 和显示资源；其他战斗起始（type 10）顺序读取服务端 `battleinfo`
中的角色字段。两条路径均没有装备 ID、装备需求等级或 `equip.dsh` 查询。故战斗不能、也不会在
开始时移除不符合等级的装备；等级合法性必须由服务端在持久化/装备登录边界维持。

探针保留到原始账号复测确认：重启服务端并重新登录后，应出现
`equipment_level_state_repair ... action=unequip-to-backpack` 和一次事务保存；角色登录装备包中不再
包含上述八个已装备行，HUD `bar_max` 与场景节点/战斗最大 HP、MP 一致。 

## 纯服务端回归

`scripts/overlevel-equipped-state-regression.c` 不启动监听器、不连接 MySQL，也不修改任何账号。
它直接调用同一加载期修复函数，验证三项不变量：34 级角色的 70 级耐久装备完整迁回背包、70 级
角色的同一装备保持穿戴、背包已满时完全保留原装备状态并明确报 `blocked`，同时验证该阻塞行
不会泄漏进客户端的 `7/7 type=2` 装备引导。运行命令：

```powershell
gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 `
  -ffunction-sections -fdata-sections -w `
  scripts/overlevel-equipped-state-regression.c src/gifDecode.c src/mystd.c `
  src/mysql-client.c src/md5.c -o obj/server/overlevel-equipped-state-regression.exe `
  '-Wl,--gc-sections' -lpthread -liconv -lm -lkernel32 -lws2_32
.\obj\server\overlevel-equipped-state-regression.exe
```

## 属性页有装备加成、战斗却没有的原因（2026-08-09 复核）

这不是战斗阶段把装备卸下。两条客户端/服务端属性链的输入在异常历史装备状态下发生了分叉：

1. `JianghuOL.CBE:scene_rebuild_status_meter_node(0x0100FED8)` 逐项遍历客户端本地
   装备管理器，唯一的有效性条件是 `currentDurability > 0`；它随后调用
   `CalcEquipStatBonus` 和 `AddActorStatBonus`。`scene_draw_status_panels(0x0101466A)` 用
   该计量对象的 `+0xC4/+0xC8` 作为显示 HP/MP 上限。因此，只要旧的 70 级装备仍留在
   客户端管理器，人物属性页/场景 HUD 都会显示其加成，客户端不会检查需求等级。
2. `mmBattleMstarWqvga.cbm:HandleBattleStartMsg(0x66CC)` 的 `type=5` 不能被笼统描述为
   “全部从场景节点取值”。它先按 `battleinfo` 的首段定位场景怪物节点，再读取第二段的
   角色行；该角色行依次是 `roleId, hp, hpMax, mp, mpMax`。本服务端的
   `vm_net_mock_build_battle_scene_start_info_blob()` 用
   `vm_net_mock_role_default_vitals()` 填写这五项，而后者使用服务端的完整有效装备计算。
   70 级装备对 34 级角色会被 `vm_net_mock_role_equipment_slot_is_usable()` 排除，故战斗
   行正确地只有 `615/925` 的有效上限。

首个错误状态仍是历史数据库里“34 级角色穿着 70 级装备”。但此前仅在 `7/7 type=2` 输出层
过滤该行还不足以清理**已运行客户端**：`mmGameMstarWqvga.cbm:sub_D04(0xD04)` 对 type 2
只将每一行经物品管理器 `+104` 合并/插入；零行 `iteminfo` 不是删除旧装备的协议。因此同一
客户端会保留修复前已经装入内存的装备，并继续重算属性页，而新开战使用的是服务端已校验的
角色行，造成“信息有加成、战斗没有”的表象。

这也修正了本文件上一节的简写：type 5 的**场景节点段**用于场景侧目标，角色战斗属性来自其后
的 `battleinfo` 角色行；二者都不包含需求等级校验。正确的后续修复必须在装备状态同步/角色
生命周期契约中让客户端本地装备管理器与服务端有效装备集合收敛，不能在战斗中临时改属性或
伪造卸装成功。对当前背包 `99/100`、客户端只公开 64 格的账号，迁回仍须先由管理员腾出
可见空间；在此之前，完全退出并重新启动客户端可作为验证“无旧内存装备缓存”的诊断步骤，
但不是持久化修复本身。
