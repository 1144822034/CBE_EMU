# 2026-09-06：跑环击杀玩家改为友好切磋计进度

phase: ring-kill-player-friendly-spar
status: implemented

## 根因 / 契约

跑环 `taskType=4`（击杀玩家）进度原先只在 **强制 PK** 终局（`!friendlyMode`）给胜者 +1。  
友好切磋拆分后，菜单与追踪已写「切磋」，但结算门闩排除了 `friendlyMode`，导致只能靠强制 PK 完成环任务。

## 修改

| 点 | 行为 |
|----|------|
| `mock_server_battle.c` 决斗终局 | `!arena && !wager && friendlyMode` 时调用 `vm_mock_activities_on_pvp_kill` |
| 强制 PK / 擂台 / 赌战 | 不计环进度 |
| 描述 `vm_mock_ring_format_desc` | `切磋战胜N名职业(等级段)玩家` |
| 匹配规则 | 不变：胜者账号落库；对手 job + bracket 须匹配 |

## 验证

1. 接「切磋战胜1名…」环 → 友好切磋匹配对手并获胜 → 进度 1/1 → 回跑环使可提交  
2. 对手职业或等级段不符 → `ring_pvp_kill_skip`，进度不变  
3. 强制 PK 打赢匹配对手 → 环进度不加  
4. 擂台 / 赌战终局 → 环进度不加  

## 关联

- `2026-08-28-ring-quest-desc-clarity.md`
- `2026-09-05-friendly-spar-vs-force-pk.md`
