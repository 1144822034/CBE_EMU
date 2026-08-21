# 装备强化玄晶概率曲线（2026-08-21）

Status: validated

## 当前卡点与首次偏离

触发方式：将一件 `+15` 背包装备放入五个六级玄晶（物品 `906`），先发送 `1/29/2`
预览，再发送同材料的 `1/29/3` 确认。预期是不足级玄晶在最高阶段只能给出有限成功率；实际
预览为 100%，确认也必然成功。

首次偏离位于服务端 `vm_net_mock_equipment_enhance_success_rate()`，而不是客户端显示或随机
判定：旧公式为 `required=(当前等级+1)*100`，每个玄晶为 `tier*100`。因此目标 `+15→+16`
只需 `1600`，但五个六级玄晶为 `5*600=3000`，在比较处被截断为 100%。

## 已确认的协议与客户端边界

| binary | function/address | 已确认行为 |
| --- | --- | --- |
| `江湖OL.CBE` | `SendEquipSequenceReq(0x0101DD1E)` | `1/29/2` 与 `1/29/3` 均发送同一 `equipseq + occultinfo` 材料流。 |
| `江湖OL.CBE` | `HandleItemUseAndEquip(0x01028C7C)` | `29/2` 读取服务端 `value`、`money`；`29/3` 读取服务端 `result` 及已消耗材料。客户端不从材料自行裁定成功率。 |

请求与回复格式均已由 `docs/re/2026-07-19-equipment-enhancement.md` 固定：

- `29/1` 的 `data1` 是各目标强化级的材料需求，`data2` 是各级玄晶的贡献；
- `29/2.value` 是服务端给 UI 的百分比；
- `29/3` 必须使用与 `29/2` 相同的曲线决定 `result=1|2`，不增加字段、不改变网络事件。

本轮可用的 MCP 工具集中没有已连接的 IDA 实例；上述函数地址来自项目中已保存的 CBE 静态
反编译取证。概率具体数值不在客户端资源或 parser 中，属于本模拟服务端明确设定的平衡规则，
不应伪称为原服数值。

## 已确认的服务端平衡规则

设目标等级为 `T = 当前强化等级 + 1`，单个 `N` 级玄晶的贡献为：

```text
crystalPower(N) = 3^(N - 1)
requiredPower(T) = 3^(T - 1)
rate = min(100, floor(sum(crystalPower) * 100 / requiredPower))
```

因此每向下一级，单颗玄晶贡献恰好变为前一级的 `1/3`。高于目标等级的玄晶仍会使总和至少
达到 100%，但不会超过 100%。`29/2.value` 是协议中的整数百分比，所以页面显示会向下取整；
`29/3` 用同一个整数分母抽签，不丢弃小数概率。例如单个十五级玄晶冲 `+15→+16` 在页面显示
`33%`，实际成功概率为精确的 `1/3`。

`29/1.data1` 与 `data2` 复用这两个公式，所以强化页展示的需求、`29/2` 的预览和 `29/3` 的
实际判定不会分叉。

| 目标 | 材料 | 能量 / 需求 | 成功率 |
| --- | --- | ---: | ---: |
| `+1` | 1 个一级 | 1 / 1 | 100% |
| `+16` | 1 个十六级 | 14,348,907 / 14,348,907 | 100% |
| `+16` | 1 个十五级 | 4,782,969 / 14,348,907 | 33.33…% |
| `+16` | 1 个十四级 | 1,594,323 / 14,348,907 | 11.11…% |
| `+16` | 5 个六级 | 1,215 / 14,348,907 | 0.0084…% |

## 负向证据与范围

- 不能仅把 `29/2.value` 改低：`29/3` 仍按同一旧计算会实际 100% 成功，造成预览与结果不一致。
- 不修改 `29/2/29/3` 的对象、字段类型、材料扣除、铜钱、持久化事务或客户端状态。
- 抽签仍只由服务端决定；为支撑最大 `14,348,907` 的精确分母，对既有确定性
  `schedulerTick + equipSeq + level` 输入做了整数混合后再取模。若直接拿原先的很小数值取大模，
  会错误地偏向低位区间而提高成功率。
- 不借用 `CalcEquipStatBonus` 的十六档装备属性规则；它与玄晶成功率无关。

## 实现与验证

`src/server/mock_server_equipment_npc.c` 的共享 `crystal_power` 与 `required_power` 助手采用
三的幂次；材料校验、成功率计算、`29/1.data1`、`29/1.data2` 都只调用它们。`29/3` 使用相同
`required_power` 作精确整数抽签。既有 `builtin-equipment-enhance` detector、`29/2.value`、
`29/3.result`、事件投递、扣材、铜钱和持久化事务均未改变。

新增 `scripts/equipment-enhancement-crystal-probability-regression.c`。它用隔离内存角色与
真实请求编码验证：

1. `29/1` 的第 15 项需求为 14,348,907，第六级玄晶贡献为 243；
2. `+16` 的十五级与十四级玄晶分别与十六级玄晶保持精确 `1/3`、`1/9` 比例；
3. `29/2` 对一个十五级玄晶在 `+15` 返回 `value=33,money=1600`；
4. 同材料 `29/3` 分别在落于失败、成功区间的确定性抽签下返回 `result=2|1`；前者等级保持
   `+15`，后者升至 `+16`，两者均按既有语义扣除该玄晶与 1600 铜钱；
5. 同级单颗玄晶仍为 100%，初级单颗一级玄晶仍为 100%。

验证命令：

```powershell
make -j2
gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 -ffunction-sections -fdata-sections -w scripts/equipment-enhancement-crystal-probability-regression.c obj/server/gifDecode.o obj/server/mystd.o obj/server/mysql-client.o obj/server/md5.o '-Wl,--gc-sections' -o tmp/equipment-enhancement-crystal-probability-regression.exe -lpthread -liconv -lm -lkernel32 -lws2_32
.\tmp\equipment-enhancement-crystal-probability-regression.exe
```

结果：`equipment enhancement crystal probability regression passed: +15->+16 level-15 crystal contributes one third in 29/1, 29/2 and 29/3`。夹具不启动监听器、不连接 MySQL，未修改任何账号或角色数据。

## 已知边界

客户端协议只证明 `29/1` 提供两张数值表、`29/2` 消费服务端成功率、`29/3` 消费服务端结果；
原服的具体概率表不在当前资源、请求或已保存的反编译中。因此本文件的三级递减曲线是明确的
模拟服务端平衡设计，而非原服数值声称。若以后取得原服预览/确认记录，可在不改变协议格式的
前提下替换这两个共享助手，并更新此回归的期望表。
