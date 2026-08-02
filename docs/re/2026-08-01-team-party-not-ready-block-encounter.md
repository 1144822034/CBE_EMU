# 组队：队友未准备好时队长遇怪不进战

日期：2026-08-01

## 现象

队友停在商城（mmShop）界面时，队长点怪进入组队战：

- 商城队友仍停在商城，收不到可用的战斗 operate 路径
- 队长与已在地图的队友卡在组队战回合屏障

日志特征：`team_battle_queue` / `mock_team_battle_start` 之后，商城座仍有 `shop_open` / `shop_return`，并出现 `awaiting-battle-1/7/14-confirm` 一类 shop-return 与战斗确认交错。

## 根因

`team_collect_battle_members` 只按同场景可见 + 存活拉人，不区分 mmShop / shop-return。  
商城座的 `sceneVisible` / `onlineScene` 仍挂在队伍地图上，会被写进 `1/4/5` 并 `team_begin_battle` 排队；该座客户端未进入 Battle.cbm，队长侧回合永远等不齐。

## 契约

同场景、存活的队友必须已离开 mmShop 且 `sceneVisibleReady`（非 scene pending）后，
队长 `4/1` / 挂机才允许开战。任一未准备好 → **整队不进战**（不降级成队长单人战）。

出店后的 loading-clear / kind2 / catalog / busy_ack **不**算未准备
（见 `2026-08-01-party-not-ready-after-shop-exit.md`）。

## 修改

1. `vm_mock_service_session_team_battle_not_ready_reason`：仅 mmShop 壳 + 场景未 ready
   （及 reseed 未完成且场景未 ready）。
2. `vm_mock_service_team_party_not_battle_ready`：同场景用 `sceneVisible` 或 `onlineScene` 判定；存活座任一未准备好则拦截。
3. 队长 `4/1`：回 `2/10+25/12+1/3/3`（系统提示「队友未准备好…」），日志
   `action=reject-party-not-ready`。**禁止**未请求 `25/11`（会粘「斗」字，
   见 `2026-08-01-party-not-ready-sticky-dou.md`）。
4. 挂机 start / start-delay / poll：同样拦截；start 带清条+聊天并 `hangup_loop_clear`。
5. `team_begin_battle`：防御性二次检查，未准备好则不武装 `battleActive`。

## 验证

1. `make -j2`
2. 两人组队：队友进商城 → 队长点怪 → `reject-party-not-ready`，**无**粘「斗」，
   系统聊天有提示。
3. 队友退出商城（地图可走）→ 队长点怪 → 正常 `team_battle_queue` / `mock_team_battle_start`。
4. 队友在其他地图：不因该座拦截；行为与原先「不同图不拉进战」一致。
5. 单人 / 未组队不受影响。
