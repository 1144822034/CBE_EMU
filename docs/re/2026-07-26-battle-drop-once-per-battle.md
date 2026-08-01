# 怪物掉落：每场战斗掷一次

日期：2026-07-26

## 变更

`vm_net_mock_battle_grant_reward_once()` 中每个掉落行的投掷，由「按本场击败敌人数各掷一次并累加」改为「每场战斗只掷一次；命中则发放 1 个（任务材料仍按剩余需求封顶）」。

经验 / 铜钱仍按 `enemyCount` 缩放，不受本变更影响。

## 修改点

- `src/server/mock_server_equipment_npc.c`
- `docs/re/2026-07-24-monster-multi-drop-admin.md`（对应描述已同步）
