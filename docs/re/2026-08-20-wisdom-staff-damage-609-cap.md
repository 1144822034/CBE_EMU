# 智慧法杖后法术仍显示 609 伤害

## 复现

角色脱下装备后以 `Operate=203` 施放技能，再穿戴智慧法杖 `12013`，对怪物 `30` 施法。
两次战斗画面均显示 609 伤害。

## 运行时证据

`bin/server_out.txt` 记录了完整顺序：

1. `mock_item_equip ... item=12013 ... slot=0 ... result=1`：法杖已正常入装备槽；
2. 随后的 `mock_scene_monster_moveinfo actor=30 ... hp=609/609`：该目标每只只有 609 HP；
3. 同一战斗的 `mock_battle_operate ... operate=203 ... amount=609`：该字段是本次 actioninfo
   的实际 HP 扣减值，不是未封顶的理论法术伤害。

## 数据与计算链路

`skill.dsh` 中技能 `201`（客户端操作值 `203`）具有：基础伤害 `30`、敌方单体目标、MP
消耗 `5`、智慧系数 `110`。`vm_net_mock_battle_player_skill_damage_to_enemy()` 先通过
`vm_net_mock_role_build_player_stats()` 取得包括耐久装备的智慧值，再由
`vm_net_mock_battle_skill_raw_damage_from_stats()` 计算伤害；最后必须执行：

```
finalDamage = min(damage, enemyHpCurrent)
```

这个封顶值正是 WT `4/6` 的 actioninfo 消耗的目标 HP 变化量。将其改成理论伤害会让客户端
显示与实际 HP 变化不一致，因此不是可修复的缺陷。

## 隔离回归

新增 `scripts/battle-wisdom-equipment-damage-regression.c`，不监听端口、不连接 MySQL，直接
使用当前 `equip.dsh` 与 `skill.dsh` 建立装备前后两个权威角色快照。2026-08-20 的输出：

```
staff=12013 wisdom=843->999 skill=201 coeff=110
raw=957->1412 target9999=725->1069 target609=609->609
```

由此同时证明：智慧法杖提高了原始伤害和高血量目标的最终伤害；对 609 HP 目标两次均显示
609，是两次均一击击杀的正确结果。

验证命令：

```powershell
make -j2
gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 -ffunction-sections -fdata-sections -w scripts/battle-wisdom-equipment-damage-regression.c obj/server/gifDecode.o obj/server/mystd.o obj/server/mysql-client.o obj/server/md5.o '-Wl,--gc-sections' -o tmp/battle-wisdom-equipment-damage-regression.exe -lpthread -liconv -lm -lkernel32 -lws2_32
.\tmp\battle-wisdom-equipment-damage-regression.exe
```

该夹具的无 MySQL 可选状态读取告警不参与断言；核心结果为资源加成、技能系数和两个目标 HP
边界下的伤害数值。
