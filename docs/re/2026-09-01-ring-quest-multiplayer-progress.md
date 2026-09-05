# 跑环进度多玩家漏检修复（2026-09-01）

## 现象

玩家多时，按任务要求做了多次（击杀/拜访/世界真心话），只有偶尔几次才会计入进度。

## 根因

1. **进程全局 `g_vm_mock_ring_rt`**：拜访环原先只读该缓存。其他玩家的 WT47 状态/接环会覆盖它，当前玩家对话时看到的是别人的环类型 → 漏检。
2. **击杀 ID 与接环去重不一致**：接环按显示名去重只保留一个 `enemyId`，战斗胜利可能报到同名另一 catalog id → 严格 `targetId==enemyId` 漏计。
3. **进度回写整行**：真心话等路径在 UPDATE progress 后又经 `save_runtime` 用全局 rt 整行写回，可能把别人冲过的缓存写回本账号。

游戏协议虽在 game thread 串行，但请求之间会切换活跃账号，全局 rt 不能作为进度权威。

## 修复

- 拜访 / PvP / PvE / 真心话：一律按账号+角色读 `account_role_ring_daily`，只 `UPDATE progress=…`。
- 仅当活跃会话正是该角色时，才把行同步进 `g_vm_mock_ring_rt`。
- 击杀匹配：`targetId==enemyId` **或** 后台/战斗显示名相同。
- 组队击杀优先用 `team->battleEnemyId`，减轻全局战斗槽被覆盖。

## 验证

1. 两人同时在线：A 接拜访环，B 反复打开跑环面板；A 找 NPC 对话应立刻可提交。
2. 接「击杀 N 场某同名怪」：打地图上该名任意 id 的怪，每场 +1。
3. 世界真心话：发对句子后 progress=1，且不影响其他在线玩家环数据。
4. 日志：`evidence=account-db-not-global-rt` / `account-db+name-match` / `world-chat-db-only`。
