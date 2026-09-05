# 2026-09-04：切磋发起冷却下线再登录被清掉

phase: spar-initiate-cooldown-survive-relogin
status: fixed

## 症状

附近切磋成功开战（或替身人偶抵挡）后，发起方进入 5 分钟冷却（`4/14 result=9`）。
玩家下线再登录后，冷却立即消失，可立刻再发起。

## 根因

冷却写在 **TCP session** 字段 `sparInitiateReadyMs`（`scheduler_get_tick_ms()` 相对 tick）：

1. 开战/人偶抵挡时：`session->sparInitiateReadyMs = tick + 5min`
2. `session_mark_offline` 无条件 `sparInitiateReadyMs = 0`
3. 再登录得到新的/已清零的 session → 门闩消失

冷却契约归属是 **角色（roleId）**，不是连接会话。会话生命周期清场把角色冷却一并抹掉。

## 修复

| 点 | 行为 |
|----|------|
| `mock_server_core.c` | `g_vm_net_mock_spar_initiate_cd[]`：按 `roleId` 存墙钟 `readyUnix` |
| `mock_server_guild.c` | `4/14` 检查/武装改走 `vm_net_mock_spar_initiate_cd_*` |
| `mock_server_equipment_npc.c` | 删除 session 字段 `sparInitiateReadyMs` |

- 成功开战 / 人偶 `result=8`：仍武装 5 分钟
- 忙碌/城镇/等级等失败路径：仍不武装
- 进程重启会丢掉剩余冷却（5 分钟临时态，不落 MySQL）

## 验证

1. A 切磋 B 开战 → A 5 分钟内再发起 → `result=9`
2. A 下线再登录，冷却未满 → 仍 `result=9`
3. 满 5 分钟后可再发起
4. B 持人偶抵挡后，A 下线再登录 → 冷却仍在

## Negative

- 不得把冷却绑回 session tick
- 不得在 offline 路径清 roleId 冷却表
- 比武擂台仍不走本附近切磋冷却门闩
