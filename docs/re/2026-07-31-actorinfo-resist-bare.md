# actorinfo 抗性改为 bare，避免面板 2 倍（2026-07-31）

## 反馈

玩家属性面板「抗性」约为装备抗性和的 **2 倍**。

## 链路

```text
equip.dsh 抗性变化
  → 服务端 collect_equipment_bonus → playerStats.resist（战斗权威，全量）
  → actorinfo word[5] → parse → actor+0x12c
  → 同时 7/7 穿戴后 JianghuOL.CBE:0x0100FEC6 再把装备抗性加到 +0x12c
  → 属性页 paint 读 +0x12c
```

## 第一次偏离

旧 wire：`actorAttrWords[5] = playerStats.resist`（已含全部装备抗性）。

客户端穿戴 apply（`0x0100FF72` → `0x0100FEC6`）对 `actor+0x12c` 执行：

```text
ldrh [actor+0x120+#0xc] ; +0x12c 抗性
adds 装备抗性
strh
```

证据：`fec6` 在 `0x0100FF22..FF26` 写 `[r0,#0xc]`；`r0 = actor+0x120`。

强化 jump 表确实不写 `+0x12c`，但 **fec6 基表路径会写**。因此「无 jump→抗性」不能推出「抗性只靠 actorinfo」。

对比已正确的 bare 字段：力/敏/`护甲 word[4]` / HP·MP baseMax。

## 根因

actorinfo 下发全量抗性，客户端 wear-apply 再加一次装备抗性 → 面板 2 倍。  
战斗结算只读服务端 `playerStats.resist`，不受面板半字影响。

## 修改

`src/server/mock_server_interaction_login.c`：

```text
actorAttrWords[5] = 0   # bare；可用 CBE_ACTOR_ATTR_RESIST 覆盖
actorGap0CC8      = 0   # 与活力 gap 对齐；非 paint-8 抗性半字
```

战斗 / `build_player_stats` 的 `resist = equipment.resist` **不改**。

## 验证

- `make -j2`
- 穿若干带「抗性变化」的装备：面板抗性 ≈ 各件抗性之和（与装备详情一致），不是 2×。
- 日志 `mock_actorinfo_attrs ... resist_word=0 ... resist=<全量>`。
- 法术承伤仍按全量 `playerStats.resist`（百分比上限 70%）。

## 残留

- **命中** `word[2]` 仍下发 `101+equipment.hit`，而 `fec6` 也会加装备命中 → 面板命中可能偏高（`101+2×装备命中`）。未在本轮改；若反馈命中异常，应按同样 bare 策略改为 `101` 或 `0`。
- 强化里程碑「护甲(抗性向)」进服务端 `armor` 而非 `resist`；与本次双计无关。
