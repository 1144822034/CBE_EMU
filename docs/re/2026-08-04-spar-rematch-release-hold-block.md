# 2026-08-04：连续切磋第二场异常 / 第三次同意卡住

## 触发

角色 **455**（lxh002）对 **1**（lxh001）连续切磋（用户口述为 456/455；当次控制台切磋对象为 455↔1，456 未出现在 `mock_spar_*` / `duel_*` 链路上）。

1. 第一场完整进出战斗。
2. 第二场：服务端 `duel_begin serial=2` 且双方 `duel_start_deliver`，但 455 侧无 `duel_round_commit`，随后 `duel_round_timeout_autofill` 一回合结束。
3. 第三场：邀请仍 `queued=1`，对方 `result=1` 同意，但 `accepted=0 duel=0 start=0 resp=5`，邀请方收到 `spar-result result=2`；稍后 `duel_release serial=2`，455 `heartbeat-timeout`。

## 证据（控制台）

```
duel_terminal_deliver serial=2 ... release_not_before=5723
mock_spar_request ... queued=1 result=1
mock_spar_reply ... result=1 accepted=0 ... duel=0 start=0 resp=5
social_notice ... spar-result result=2
duel_release serial=2
session_offline ... role=455 ... reason=heartbeat-timeout
```

第二场前同样在 hold 内发起邀请，但同意发生在 `duel_release serial=1` 之后，故 `accepted=1`；第三场同意仍落在 `release_not_before` 窗口内。

## 根因

1. 双方拆场包投递完后，`releaseNotBeforeTick` 被设为 `CBE_DUEL_EXIT_REDELIVER_TICKS`（默认 **300**）。
2. 该窗口内 duel 槽仍 `active`，`duel_find_for_client` 非空 → `duel_begin` 失败。
3. 邀请路径**不检查**该 hold，仍可排队弹窗；同意后 `accepted` 被打成 0，却按拒绝通知邀请方（`spar-result=2`），应战方只得空 WT → UI/状态卡住直至心跳掉线。
4. `sparExitRedeliverUntilTick` 本应在 **slot 已释放** 后为 operate 补拆场服务；把 release hold 绑到同一 300 tick 反而使 rematch 与 post-release 互相妨碍。

第二场“没进战斗”（455 视角）：服务端已投递 `duel_start`，但 455 未提交 operate 被超时补刀。与第三次 hold 阻塞是不同症状；本次先修 hold 导致的同意失败/卡住。

## 修改

- 拆场完成后 `releaseNotBefore` 改为短 grace（`CBE_DUEL_RELEASE_GRACE_TICKS`，默认 8）。
- `duel_begin` / 切磋邀请：若仅剩 finished-hold，强制 `duel_release` 再开战或允许邀请。
- 仍在进行中的 duel（未 finished 或仍有 start/terminal/action）拒绝新邀请（`result=3`）。
- 开战时清掉双方 `sparExitRedeliverUntilTick`，避免旧拆场 operate 路径干扰新场。

## 复测

1. A/B 打完一场后立即再邀再同意：应 `accepted=1` + `duel_begin` + 双方 start，不得再出现 hold 内 `accepted=0`。
2. 战斗中再邀：邀请方 `result=3`，不排队。
3. 连续三场均可进出战斗；观察 `duel_release ... reason=rematch-begin-*` 或短 grace 后的 `release-if-done`。
