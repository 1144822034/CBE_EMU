# 2026-08-11：替身人偶拒切磋 / 发起冷却

phase: spar-substitute-doll-and-initiate-cooldown
status: fixed

## 契约

1. **商城道具** `item.dsh:846`「替身人偶」
   - 类别 14（秘宝）
   - 酷宝价 20
   - 耐久 1；背包 `count` = 剩余抵挡次数（可叠加）
2. **拒切磋（不进战）**
   - A 对 B 发附近切磋 `4/14`，若 B 背包有 846：自动扣 1，**双方均不进切磋场景**
   - 发起方 tip / `4/14 result=8`：`对方替身人偶生效，无法切磋`
   - 被切磋方系统提示：`替身人偶生效，抵挡了一次切磋`
3. **发起冷却**
   - 成功开战，或**人偶抵挡**（result=8）：发起方 `sparInitiateReadyMs = now + 5min`
   - 冷却中再发起 → `4/14 result=9`：`切磋冷却中，请稍后再试`
   - 忙碌拒绝等其它失败路径 **不**开冷却

## 修改

| 点 | 行为 |
|----|------|
| `item.dsh` (+Pixi/web/JianghuOL 副本) | 新增 846 |
| `mock_server_core.c` | `SPAR_SUBSTITUTE_DOLL_*` / `SPAR_INITIATE_COOLDOWN_MS` |
| `mock_server_role.c` | 购买入库：耐久叠加入 count |
| `mock_server_equipment_npc.c` | `try_consume_spar_substitute_doll` + session 冷却字段 |
| `mock_server_guild.c` | `build_nearby_spar_request_response` 门闩 |
| `protocolSocial.ts` | result 8/9 tip |

## 验证

1. 商城秘宝页可见「替身人偶」，购买后背包数量 +1
2. B 持有人偶，A 切磋 B → 双方提示、不进战、B 数量 -1，且 A 进入 5 分钟冷却
3. B 无人偶 → 仍可开战；A 开战或被人偶抵挡后 5 分钟内再发起 → result=9
4. 忙碌拒绝 / 城镇 / 等级门闩仍不开冷却

## Negative

- 不得把人偶当成切磋死亡惩罚抵免
- 不得在未开战路径扣人偶
- 比武擂台 `duel_begin` 不走本附近切磋人偶/冷却门闩
