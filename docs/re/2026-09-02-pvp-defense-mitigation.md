# PvP 护甲减伤曲线（2026-09-02）

## 目标与范围

高阶装备的护甲基础值会被完整累加，强化还会按客户端已确认的逐级规则继续提高防具
护甲。原先 PvP 与 PvE 共用 `attack * 100 / (100 + defense)`，当切磋目标拥有数千至数万
最终防御时，伤害会收敛到个位数。

本次只调整切磋/擂台共用的服务端 PvP 伤害结算；不修改 `equip.dsh`、角色属性、客户端
二进制、WT 包字段或网络事件投递。PvE 随后由独立的 V7 PvE 曲线处理，不会调用本 PvP
辅助函数。

## 已采用公式

令 `raw` 为普通攻击或攻击型技能在防御前算出的伤害，`defense` 为目标最终防御：

```text
reduction = 75% * defense / (defense + 2000)
pvpDamage = max(1, round(raw * (1 - reduction)))
```

等价的整数形式为：

```text
pvpDamage = max(1, round(raw * (defense + 8000)
                               / (4 * (defense + 2000))))
```

所以护甲减伤是单调递增、边际递减的，并且理论上不会超过 75%。它不要求新增护甲穿透
属性，也不把抗性并入物理减伤。

| 最终防御 | 原始伤害 1000 的 PvP 结果 | 减伤 |
| ---: | ---: | ---: |
| 1,000 | 750 | 25.0% |
| 3,000 | 550 | 45.0% |
| 6,000 | 438 | 56.2% |
| 9,300 | 383 | 61.7% |
| 34,500 | 291 | 70.9% |

## 调用边界与验证

`vm_mock_service_duel_damage()` 在普通攻击和攻击型技能已经得出 `raw` 后，唯一调用新的
PvP 减伤辅助函数。它仍下发相同的 `4/6` 行动记录，客户端继续仅消费服务端给出的目标和
最终伤害；没有新增或重排回调、事件或响应对象。

独立回归 `scripts/pvp-defense-mitigation-regression.c` 覆盖零防御、四个装备实测档位、
接近理论上限的防御值，以及 `UINT32_MAX` 的算术边界。PvE 公式及怪物重算由
`scripts/pve-defense-mitigation-regression.c` 独立验证。

运行方式：

```powershell
make -j2
gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 -ffunction-sections -fdata-sections -w scripts/pvp-defense-mitigation-regression.c obj/server/gifDecode.o obj/server/mystd.o obj/server/mysql-client.o obj/server/md5.o '-Wl,--gc-sections' -o obj/server/pvp-defense-mitigation-regression.exe -lpthread -liconv -lm -lkernel32 -lws2_32 -ldbghelp
.\obj\server\pvp-defense-mitigation-regression.exe
```
