# 组队商城返回后无队伍列表 / 队长开战队友闪退

日期：2026-08-01

## 现象

- 队友进商城返回后看不到队伍列表；队长仍可见。
- 服务端仍认为在队：队友遇怪提示在队伍中。
- 队长遇怪后队友闪退；队长战斗界面卡住后退出，无法执行动作。

## 证据（`debug-6d7cbd.log`）

1. `shop_return_solo_roster` client=2915432218 `pending=1`（solo 5/10）。
2. `shop_return_rehydrate_pre_peers` `peersPending=1` → `shop_return_peers_queue queued=1`。
3. 其后**没有** `team_member_join_5_5`；poll 被 shop-return / loading-clear 占用。
4. `team_deliver` resp=215 投给仍无 roster 的队友 → 闪退；队长卡在回合屏障。

## 根因

`finish_shop_return_rehydrate` 正确排队了 `TEAM_MEMBER_JOIN`（5/5），但 scene-sync
把社交通知放在 nearby 分支末尾；shop-return 的多发 30/2 / catalog / busy_ack
（以及随后的 team battle start）抢先 `return`，5/5 饿死。

## 修改

1. poll 在 shop-return busy_ack 之后、map-stone/hangup/team-battle 之前，增加
   `shop_return_team_peer_join` 优先投递（scene ready、非 mmShop、loading-clear 空闲）。
2. rehydrate 先排队 5/5 再 HSP；社交选择优先 JOIN 于 HSP。

## 验证（post-fix）

1. 出商城日志顺序：`shop_return_solo_roster` → `shop_return_team_peers_queue`
   →（loading-clear/busy_ack 结束后）`shop_return_team_peer_join_poll` +
   `team_member_join_deliver ... update=5/5`。
2. 队友客户端恢复队伍列表；队长仍可见。
3. 队长遇怪：双方进战斗，无队友闪退、无队长卡回合屏障。
4. 2026-08-01：该优先投递曾在功能还原中丢失，已按本文重新落地。
