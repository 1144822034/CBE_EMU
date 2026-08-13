# 无情谷北挂机续战调查（2026-08-13）

## 现象

用户报告在“无情谷北”点击挂机后，战斗结束没有自动进入下一场。

## 本轮已核对的场景与运行证据

- 场景名严格为 `17无情谷_01.sce`；该场景的挂机怪物是 `94`（黄色蜜蜂群）。
- 最新 `bin/server_out.txt` 中，角色 `10036` 的两次真实挂机均不是胜利收口：

  ```text
  scene_hangup_start ... scene=17无情谷_01.sce battle=2 ... source=request
  mock_hangup_battle_start ... enemy=94 enemies=2 rolehp=615/615 enemyhp=2656/2656
  mock_death_penalty ... respawn_scene=17无情谷_01.sce
  scene_hangup_stop ... battle=2 restart_pending=0 reason=scene-pending

  scene_hangup_start ... scene=17无情谷_01.sce battle=3 ... source=request
  mock_hangup_battle_start ... enemy=94 enemies=3 rolehp=615/615 enemyhp=3984/3984
  mock_death_penalty ... respawn_scene=17无情谷_01.sce
  scene_hangup_stop ... battle=3 restart_pending=0 reason=scene-pending
  ```

- 同一日志末尾的 `mock_battle_settle ... victory=1` 属于随后一个
  `mock_challenge_battle_start ... auto=0` 的手动场景碰怪，不是挂机场次，不能用来证明挂机续战失败。

## 已确认的客户端/服务端生命周期

客户端的安全边界为：

```text
挂机 4/5 + 4/11(type=1)
  -> 胜利 4/7
  -> 客户端关闭奖励面板
  -> 空 25/5
  -> scene_hangup_round_complete
  -> 5 秒后 scene poll
  -> 下一场 2/2 + 4/5 + 4/11(type=1)
```

`mmBattle` 的奖励面板仍存活时不能提前下发下一份 `4/5`：既有运行时证据表明这会重入旧
BattleScreen，导致卡死或动画循环。`bin/multiplayer/start-player-common.bat` 已默认开启
`CBE_HANGUP_AUTO_CONFIRM=1`，它通过一次真实的模拟器触摸让客户端自己产生 `25/5`，不修改
下行协议或客户内存。

## 当前结论

本轮现有无情谷北日志显示的是“挂机战斗失败后复活”，而非“挂机胜利后未续战”。服务端在死亡
导致场景重入时清除在线挂机会话是正确行为，不能为了续战而把死亡场次继续建战。

在修改续战逻辑之前，必须取得一条**挂机胜利**的同场景链路。届时应在日志中依次检查：

```text
mock_hangup_battle_start ... scene=17无情谷_01.sce
mock_battle_settle ... victory=1
scene_hangup_round_complete ... scene=17无情谷_01.sce
mock_hangup_battle_start source=scene-poll ... scene=17无情谷_01.sce
```

若第二、三行之间缺失，则检查客户端是否启用了自动确认及是否真实发送 `25/5`；若第三行存在而
第四行缺失，则再针对服务端的 five-second poll continuation 修复。这样可避免把死亡、奖励面板
未关闭和续战状态错误混作同一个问题。
