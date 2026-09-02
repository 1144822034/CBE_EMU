# PvE 护甲与法术减伤曲线（2026-09-02）

## 根因与范围

原 PvE 与旧 PvP 共用：

```text
damage = max(1, round(raw * 100 / (100 + defense)))
```

它的拐点只有 `100`。因此终局常见的几千到上万最终护甲会把同级怪物的物理伤害压到
个位数，同时旧的默认怪物血量和攻击又是按该公式反推，无法独立调整角色减伤与 PvE
击杀/生存节奏。

本次只改服务端 PvE 数值结算与其默认怪物生成；不修改 `equip.dsh`、角色可见属性、
CBE/CBM 二进制、WT 响应字段或网络事件投递。切磋/擂台继续使用独立的 75% 上限 PvP
曲线。

## 已采用的物理伤害公式

对内容等级 `L`，从 `equip.dsh` 的品质 0 全套装备按职业保守基准取得该等级的预期防御，
并采用下列强化档：1--20 为 `+0`、21--40 为 `+4`、41--55 为 `+6`、56--65 为 `+8`、
66--70 为 `+10`。为避免职业换装点使高等级反而变弱，每一项参考属性都取至当前等级的
单调历史最大值。

```text
K(L)       = max(200, round(expectedDefense(L) * 0.30))
armorDR    = 85% * defense / (defense + K(L))
pveDamage  = max(1, round(raw * (1 - armorDR)))
```

等价整数比例是：

```text
pveDamage = max(1, round(raw * (20K + 3D) / (20 * (D + K))))
```

它使护甲减伤单调递增、边际递减，理论上不超过 85%。从项目当前装备表计算，70 级的
预期防御为 11,735，故 `K(70)=3,521`：

| 等级 | 最终护甲 | 原始伤害 | PvE 最终伤害 | 减伤 |
| ---: | ---: | ---: | ---: | ---: |
| 70 | 250 | 1,000 | 944 | 5.6% |
| 70 | 5,000 | 1,000 | 501 | 49.9% |
| 70 | 11,735 | 1,000 | 346 | 65.4% |
| 70 | 34,500 | 1,000 | 229 | 77.1% |
| 70 | 极大值 | 1,000 | 150 | 85.0% |

## 怪物法术与默认数值

既有灵体、元素两类怪物被识别为法术攻击者。现在它们的伤害结算为：

```text
magicDamage = max(1, floor(raw * 1000 / (1000 + resistance)))
```

法术路径**完全不读取护甲**；物理怪物继续使用上述 PvE 护甲公式。默认灵体/元素怪物的
攻击值也按同级预期抗性反推，避免去掉护甲后产生意外的伤害跳升。

默认怪物曲线升级为 V7：普通怪与首领继续沿用各十级档既定的击杀/生存目标，但血量、
攻击和防御改由新公式重新计算。数据库迁移 `monster-pve-defense-v7` 仅更新四项战斗值
（HP、MP、攻击、防御）仍与 V6 默认公式完全一致的 `server_monsters` 行；手工编辑过的
任意一项战斗值会整行保留，掉落和经验/金钱也不在该迁移范围内。

## 验证

`scripts/pve-defense-mitigation-regression.c` 使用生产服务端代码和真实 `equip.dsh`，不启动
监听器、不连接数据库或客户端。它覆盖：

- 1 级与 70 级的动态拐点和 85% 上限；
- 70 级五个固定护甲样本；
- 1--70 预期装备属性的单调性；
- 70 级普通怪 7 次击杀/16 次生存，以及首领 36 次击杀/9 次生存目标；
- 同一法术攻击在 0 与 100,000 护甲下均只受 1,000 抗性影响，且物理分支确实不同。

运行方式：

```powershell
make -j2
gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 -ffunction-sections -fdata-sections -w scripts/pve-defense-mitigation-regression.c obj/server/gifDecode.o obj/server/mystd.o obj/server/mysql-client.o obj/server/md5.o '-Wl,--gc-sections' -o obj/server/pve-defense-mitigation-regression.exe -lpthread -liconv -lm -lkernel32 -lws2_32 -ldbghelp
.\obj\server\pve-defense-mitigation-regression.exe
```
