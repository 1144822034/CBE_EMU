# 组队战斗：最后队员行动后 `4/2` 未处理

Date: 2026-08-09

## 触发与证据

两名存活队员（`alive=03`）参加一场三怪战斗。`bin/server_out.txt` 的原始顺序为：

```text
team_battle_round_prepare ... source=4537ea06 actor=0 acted=00 alive=03 resolve=0
mock_battle_operate ... operate=233 target_mode=4 targets=3 ... actions=1 ... resp=148
team_battle_round_capture ... info=84 ...
team_battle_round_defer ... resp=5
team_battle_round_prepare ... source=cf2a9eaa actor=1 acted=01 alive=03 resolve=1
team_battle_round_prepare ... source=cf2a9eaa actor=1 acted=01 alive=03 resolve=1
unhandled wt=4/2 len=36 objects=1 first=1/4/2:27
```

因此回合屏障本身没有提前释放：第二名存活队员提交时，`resolvesRound=1` 正确。首个
错误状态是此真实 `WT 1/4/2 { index, Operate }` 在返回 `1/4/6` 前构建失败，导致分派器最终
报为未处理；怪物回合从未获得可解析的释放包。

## 根因

`vm_net_mock_build_battle_operate_response()` 与其 wire-layout adapter
`vm_net_mock_build_battle_operate_response_fallback()` 都把暂存 `actioninfo` 固定为 128 字节。

最后行动者必须在同一份 `4/6` 内构建完整的一轮：本次是三目标技能动作（首位队员已经证明其
`actioninfo` 为 84 字节）加上三只存活怪物的反击记录。`append_battle_actioninfo_record()` 的
一条普通单目标反击编码为 27 字节，所以最小组合为 `84 + 3 * 27 = 165` 字节，必然超过 128。
动作记录 append 返回失败发生在 `mock_battle_operate` 日志之前，因此两种 builder 都返回 0。二者
随后都被 dispatch 尝试，故日志中出现两次 `team_battle_round_prepare`，但没有
`team_battle_round_release`。

这不是应跳过队友或强制推进怪物回合的时序问题。客户端
`mmBattleMstarWqvga.cbm:HandleBattleActionMsg(0x6EB0)` 以 `4/6.actionnum/actioninfo` 建立
动作队列；在全队回合完成时缺失这一对象，客户端只能停留在等待状态。

## 修复

两个 `4/2` action builder 现在都使用已经定义、并由队伍回合捕获器接受的
`VM_MOCK_SERVICE_TEAM_BATTLE_ROUND_ACTION_INFO_MAX`（512 字节）作为暂存上限。该常量此前已
用于每名队员的回合缓存与合并缓冲，因此修复的是同一协议对象在生成与合并两端容量不一致，
不改变 `actionnum`、动作顺序、队伍屏障或客户端状态。

## 验证

- `git diff --check` 通过。
- `make -j2` 于 2026-08-09 通过，生成 `bin/jh-online-server.exe`。
- 尚未启动或替换用户正在运行的服务端。需用原始两客户端步骤复测：首位队员仍收到 5-byte
  defer ack；第二位队员应得到 `builtin-battle-operate`，并出现
  `team_battle_round_release` / `team_battle_state ... release=1`，不再出现 `unhandled wt=4/2`。
