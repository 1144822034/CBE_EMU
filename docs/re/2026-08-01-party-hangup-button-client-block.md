# 组队挂机按钮被客户端硬拦

日期：2026-08-01

## 现象

组队后队长按挂机软键，界面提示：

> 你在队伍中，不能使用挂机战斗

服务端无 `builtin-hangup-battle-start` / `Type=2` 日志。

## 根因

该文案在客户端资源中硬编码，**不由 mock server 下发**：

| 位置 | 偏移 |
|------|------|
| `bin/CBE/江湖OL.cbe` | `0x174EA` / `0x2CE52` |
| `bin/JHOnlineData/mmBattleMstarWqvga.cbm` | `0xA6D1` |

`HandleBattleEnterReq(0x01015E14)` 发 `2/10 Type=2` 之前，组队态已在本地拦截。  
AGENTS / RE 规范禁止改 CBE/CBM 绕过该分支，故**无法让挂机软键在组队时发出 Type=2**。

同簇文案还有「在该副本中…」「在城市中…」「此场景不能设置挂机」——均为本地场景/状态门闩。

## 契约调整（服务端）

| 角色 | 行为 |
|------|------|
| 队员 | 软键仍被客户端拦；若偶发 Type=2，服务端仍 `hangup-reject-non-leader` |
| 队长 | 软键仍被客户端拦；用**战内自动（4/11 type=1）/ 跨场 prefer** 武装 `HangupLoop*`，胜后 poll 投递挂机开战 → `team_begin_battle`（与先前 leader-hangup 开战接线相同） |

入口：

1. 队长 `4/1` 拉怪且已 prefer → `team_start_prepare_auto` 置 `HangupLoopActive`
2. 队长组队战中点自动 → `auto11` 置 `HangupLoopActive`，系统提示「已开始挂机」
3. 关自动 / type=0 → 清环（既有路径）

## 验证

1. `make -j2 server`
2. 组队后队长点挂机软键：仍见客户端「你在队伍中…」，**无**服务端 hangup start（预期）
3. 队长走怪进组队战 → 点自动：日志 `team-leader-hangup-arm` / `team_leader_hangup_arm`；胜后约间隔再 `mock_hangup_loop_poll_deliver` + `source=leader-hangup`
4. 队员点自动：不应武装全局挂机环；无队长连战

## 相关

- `2026-08-01-team-leader-only-hangup.md`（服务端队长挂机开战接线）
- `2026-07-02-hangup-battle.md`（Type=2 契约）
