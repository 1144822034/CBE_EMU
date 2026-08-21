# 2026-08-21 装备唯一抗性来源

## 触发条件与首次偏离

角色升级、切换职业或临时提高智慧后，即使没有任何带抗性的已穿装备，属性页和服务端
战斗结算仍出现抗性。首次偏离在服务端
`vm_net_mock_role_build_player_stats_impl()`：旧代码把 `wisdom / 2` 与
`endurance / 3` 加到 `equipment.resist`。这是一条未由客户端升级代码或 `equip.dsh`
证明的 mock 平衡曲线。

客户端侧取证已确认 `scene_apply_levelup_status_growth(0x01017F1C)` 只对力量、敏捷、
智慧做职业成长；`scene_rebuild_status_meter_node(0x0100FED8)` 则在解析 `1/7/7` 后把
有效装备的抗性字段直接叠入属性槽。ActorInfo 是未穿装备的基线，不能自行制造抗性。

## 修复与数据所有权

- `vm_net_mock_role_build_player_stats_impl()` 现在固定使用
  `stats->resist = equipment.resist`。装备汇总仍只接受槽位匹配、达到等级要求、耐久大于零
  的已穿实例，并保留该实例强化/附加词条的抗性。
- 战斗中的智慧临时变化不再反推抗性；临时技能携带的直接抗性字段也不再修改抗性，防止
  战斗侧产生属性页和装备状态之外的第二个来源。
- ActorInfo 删除 `CBE_ACTOR_ATTR_RESIST` 与 `CBE_ACTOR_GAP0CC8` 的覆盖入口；其基础抗性
  由同一角色统计结果提供，未装备时为零。客户端仍按既有协议从 `1/7/7` 正常叠加装备，
  没有新增、删除或重排网络字段。

因此登录属性页、重新进入场景和怪物的魔法伤害结算都读取同一条装备汇总链；脱下、损坏或
因等级不足而失效的装备会同时移除抗性。

## 回归边界

`scripts/equipment-only-resistance-regression.c` 在不启动监听器、不连接 MySQL 的夹具中验证：

1. 三个职业的 1、70、100 级未穿装备时，基础和完整统计的抗性均为零；
2. 穿上一件有效的 `equip.dsh` 抗性装备后，完整统计严格等于该装备的抗性词条，而
   ActorInfo 基线仍为零；
3. 同一件装备耐久归零或等级不满足时，抗性立即回到零；
4. 智慧与直接抗性的临时战斗修正都不会改变这条装备抗性。

该回归覆盖角色统计与战斗使用的同一对象，不修改 CBE/CBM、客户机内存、寄存器或网络包。
本次验证命令与结果为：

```powershell
make -j2
gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 -ffunction-sections -fdata-sections -w scripts/equipment-only-resistance-regression.c obj/server/gifDecode.o obj/server/mystd.o obj/server/mysql-client.o obj/server/md5.o '-Wl,--gc-sections' -o tmp/equipment-only-resistance-regression.exe -lpthread -liconv -lm -lkernel32 -lws2_32
.\tmp\equipment-only-resistance-regression.exe
```

输出：`equipment-only resistance regression passed item=3108 resist=15`。
