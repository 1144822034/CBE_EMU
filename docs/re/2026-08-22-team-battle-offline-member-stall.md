# 组队战斗：队友掉线卡住战斗场景

## 现象

三人组队战斗中一名队友掉线（heartbeat-timeout / 断线），其余玩家卡在战斗界面，
`awaitingRound` 无法解除，只能退出重登。

## 根因

`mark_offline` → `team_remove_member` 会给离队席位置 `battleMemberLeftMask` 和
`battleRoundActedMask`，但**未**调用 `team_battle_flush_round_if_ready`。

当其余成员本回合已 defer 出手、正等掉线席位的屏障时，掉线后 `acted|alive` 已齐，
却无人触发 orphan flush → 无合并 `4/6` → 客户端永久等待。

队长掉线另有一条：`leaderLeaves` 直接 `memset(team)` 解散队伍，共享战斗快照被抹掉，
存活客户端仍留在 Battle.cbm。

两人队掉线后 `memberCount<2` 会 `team_clear_battle_state`，若战斗仍在进行会把
服务端战斗状态清掉而客户端仍在战斗 UI。

## 修改

1. `vm_mock_service_team_battle_note_roster_disconnect`：与逃跑/死亡 exit 同契约
   （left+acted、publish vitals、`team_battle_round_flush`）。
2. `team_remove_member`：战斗进行中先调用 disconnect exit；队长掉线且仍有队友时
   **晋升新队长** 而非 `memset`；`roster-below-two` 清战斗快照仅当
   `!battleActive || battleFinished`。
3. Pixi：`team_leave` 通知时在战斗界面底部提示「队友已离开」。

## 验证

1. `make -j2`
2. 三人组队进战：两人先后出手 defer，第三人掉线 → 日志
   `team_battle_member_exit offline=1` + `team_battle_round_flush`；存活方收到合并 `4/6`。
3. 队长掉线、两人队一人掉线：战斗可继续或正常结算，无需重登。
4. 战后队友离队：`team_battle_clear roster-below-two` 仍仅在非进行中战斗触发。
