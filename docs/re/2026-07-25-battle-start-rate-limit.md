# 服务端战斗奖励频次限制

## 状态

已实施。限制对象是**结算发奖**（经验/金币/掉落），不是开战进场包。

## 为何不能拦开战

挂机 `HandleBattleEnterReq(0x01015E14)` 在发出 `2/10` 前已把客户端战斗状态置为 `3`。
对开战做拒绝、软 ACK 或延期投递都会让 UI 停在「获取数据」进度条；`25/11`/`25/12`
也不能可靠复位该状态（见 `2026-07-02-hangup-battle.md` 与先前失败尝试）。

因此加速器刷经验的权威约束放在结算层：战斗仍正常进入/退出，过频胜利不发奖励。

## 契约

每个在线 session 记录最近一次**成功发奖**时间 `battleRewardLastMs`：

- 默认间隔 `8000ms`（与 `CBE_HANGUP_LOOP_INTERVAL_MS` 场间默认对齐）
  - `CBE_BATTLE_REWARD_MIN_INTERVAL_MS`
  - 兼容旧名 `CBE_BATTLE_START_MIN_INTERVAL_MS`
- 关闭：`CBE_BATTLE_REWARD_RATE_LIMIT=0`（或旧 `CBE_BATTLE_START_RATE_LIMIT=0`）
- 闸门：`vm_net_mock_battle_grant_reward_once`
  - 间隔未满：标记本场已结算；**发放 1 点经验安慰值**（胜利 `4/7 result=1` 若
    EXP/金币变化全为 0，会触发
    `2026-07-24-team-battle-terminal-peer-crash.md` 同类结果面板渲染崩溃）；
    金币/掉落仍为 0（`battle_reward_rate_suppressed_serial`）
  - 间隔已满：正常发奖并 `battle_reward_rate_mark`（安慰值路径不刷新间隔锚点）
- 客户端仍走完整 `4/7` 等结算/离场路径，界面不卡进度条、不闪退

## 日志

- 抑制：`mock_battle_reward_suppressed ... reason=reward-interval`
- 超频：`battle_reward_rate_limited ... remaining_ms=`
- 发奖：`battle_reward_rate_mark ...`

## 验证

- `make -j2` 通过
- 连续战斗可正常进场/离场，不再卡「获取数据」、结算不闪退
- 6 秒内第二场胜利日志为 `reward_suppressed ... consolation_exp=1`，仅 +1 经验、
  无金币/掉落；超过间隔后恢复正常发奖
