# 组队战斗：动作播放乱序 / 叠放

日期：2026-08-05

## 症状

三人组队进战后，技能/普攻动作会乱放：上一回合动画未播完又叠下一回合，或某席看不到本回合合并 `4/6` 却已进入下一回合操作。

## 触发与证据

账号 `lxh001/lxh002/lxh003`，场景 `17无情谷_01`，组队打怪；回合 4 触发 `round-timeout` autofill。

```text
team_battle_round_release ... source=bcfae40d actions=5 resp=266
team_battle_action_queue ... action=4 delivered=04   ← 仅收刀/合成席已标投递
team_battle_round_prepare ... round=5 source=bcfae40d  ← 合成席未收到 4/6 就开下一回合
team_battle_action_deliver ... action=4 observer=176ba714
team_battle_round_prepare ... round=5 source=5f352a7a  ← 欠包席先 4/2，后才 deliver
team_battle_action_deliver ... action=4 observer=5f352a7a
mock_battle_playback_hold ... actions=5 ...
mock_battle_playback_hold ... actions=1 ...            ← 下一回合 capture 踩掉 hold
```

预期：全员先消费本回合共享 `4/6`（播完 actioninfo）再接受下一回合 `4/2`。  
实际：回合序号在 release 时已前进；欠包席与“假投递”席可立刻提交下一回合。

## 根因

1. **超时合成收刀**：`timeout_autofill` 把合并 `4/6` 建进本地 `synthResp` 后丢弃，却在 `queue_action` 里把合成席 `deliveredMask` 预标为已投递。该席从未解析本回合 actioninfo，却可开下一回合。
2. **欠包门闩缺口**：`already-acted` 路径会优先投递 pending `4/6`，但 release 后 `battleRoundSerial++` / `actedMask=0`，欠上一包的席对**新回合**不算 duplicate，`4/2` 被正常 capture。
3. **playback hold 仅挡 auto synth**：真实客户端 `4/2` 在 team 路径不看 `in_turn_gap`；收刀席连点会用 `actions=1` 的 hold 踩掉刚武装的 `actions=5` hold，第二包 `4/6` 叠进播放中。

第一次偏离在服务端契约层：共享 `4/6` 投递完成（及本席播放窗）之前就接受了下一回合操作；客户端乱放是后续症状。

## 修改

1. `queue_action`：`AutoSynthInProgress` 时不再预标合成席已投递（仍预标 absent）。
2. `synchronized_team_battle`：若本席 `observer_owes_action`，先投递 pending `4/6`（或 wait），不接受新回合 `4/2`。
3. 同路径：非 synth 且仍在 `playback_hold` 时对 operate 回 `playback-hold` wait。
4. peer 投递非终局 `4/6` 时也 `note_round_playback_hold`。
5. autofill：席位仍在 hold 则 skip，并短时重武装 deadline（`now+1`），避免再等满 20s。

## 验证

1. 三人组队；一人停手触发 timeout autofill → 日志 `action_queue ... delivered=` 不含合成席 bit；随后三人皆有 `team_battle_action_deliver`（或 operate 路径 `operate_hold_prior`）。
2. 收刀后立即连点：应见 `team_battle_round_wait ... reason=playback-hold` 或 `owes-prior-action`，不应出现下一回合 `round_prepare` 早于本回合 deliver。
3. 正常齐射（无人超时）动作顺序仍为一包合并 `4/6` 播完再开下一回合。
4. `make -j2`。

## unresolved

- 客户端是否在 wait ack 后自动重发 `4/2` 依赖 UI；若卡在操作键，需再取证重试时序。
- 高压 poll 饿死仍见 `2026-08-05-team-battle-peer-action-starve.md`。
