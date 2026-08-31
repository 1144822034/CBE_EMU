# 跑环击杀：组队参战也计进度（2026-08-31）

## 需求

跑环「击杀小怪 / 击杀 Boss」任务：组队战斗胜利时，参战队员只要各自接有匹配目标的环，也应 +1 场（或 Boss 完成），不能只计当前结算席位。

## 根因

`vm_mock_activities_on_kill_progress` 只读写进程全局 `g_vm_mock_ring_rt`（当前活跃账号的环运行时）。组队胜利结算时：

1. 仅活跃会话可能被推进；
2. 多客户端下该全局会被其他玩家的 WT47 覆盖，连活跃席也可能写错账号。

PvP 击杀环已按胜者 `accountId` 落库；PvE 击杀此前未对齐。

## 修改

- `vm_mock_ring_apply_pve_kill_for_account`：按账号+角色读/写 `account_role_ring_daily`，目标怪物匹配则推进进度；若正是当前活跃角色则同步 `g_vm_mock_ring_rt`。
- `vm_mock_activities_on_kill_progress(enemyId, isBoss, reason)`：
  - `reason` 以 `team-battle` 开头且队伍 `battleMemberCount>=2`：对 `battleMemberClientIds` 全员各记一次；`ringKillRewardedSerial` 与 `battleSerial` 闩锁，避免同场多次 `note_victory` 重复加。
  - 否则仅记当前活跃角色（仍走 DB）。
- 调用点：`vm_net_mock_battle_note_victory_settlement` 传入 `reason`。

## 验证

- 双人组队，双方都接「击杀 N 场某怪」且目标一致：打一场该怪，双方进度各 +1。
- 仅一方接该怪环：只有该方 +1。
- 同场 `team-battle-*` 多次 settlement 日志：`ring_pve_kill_progress` 每角色每场只应出现一次。
- 单人打怪：仍按场 +1，打开跑环面板进度正确。

## 风险

- 未进本场 `battleMemberClientIds` 的挂名队友不计（与同图参战契约一致）。
- 非 `team-battle` 的个人遭遇战即使在队中也不共享（仅组队战斗共享）。
