# 梦境使者出本再进：一直「地图加载中」（2026-08-03）

## 现象

使者从 `29*` 出到户外后，再点使者进本，NPC/系统消息一直提示「地图加载中，请稍候再进入。」（多试几次仍不放行；进商店再出有时又能进）。

## 根因

1. 出本 `30/1` 武装户外 `map_stone_loading_clear`（`remaining=3`）+ `dreamExitReenterSettleRequired`，并置 `direct_enter_pending` / `scene_change_target_valid`；客户端 observation 后 `sceneVisiblePending=1`，`sceneVisibleScene` 仍为 `29*`。
2. 户外 poll clear 在 `pending` / `direct_enter` / `target_valid` 任一为真时直接 `return 0`，**不消耗** `remaining`。
3. 进本门闩 `instance-enter-gate-wait-clear` 见 `remaining>0` 且 clear 场景≠梦境 → 永久提示。
4. 进商店会走 `stale_map_transfer_clear` 清传送标志，poll clear 才恢复 →「必须先进商店」旁证。

首个错误状态：出本 settle clear 被传送 in-flight 门闩挡住，无法投递 `30/2`。

## 修复

`mock_server_social.c` `build_pending_map_stone_loading_clear_response`：

- **仅当户外 `scene_ready` 且 visible==clear** 时，清残留 `direct/target` 传送标志，再投递 settle clear。
- pending 且 `scenePendingScene==clear`：**hold**（不 cancel、不早投 `30/2`）。
- clear `remaining→0` / 过期时清 `dreamExitReenterSettleRequired`。

### 续记：第二次出本卡进度条（同日）

运行时证据：

```text
dream_exit_settle_clear_unstick ... pending=1
dream_exit_settle_clear_pending_match visible=29* clear=c04*
mock_map_stone_loading_clear remaining=2   ← 过早 30/2
scene_pending / mmgame-transfer-followup   ← 随后户外 ScreenInit
```

首个偏离：pending/visible 仍为梦境时投递户外 `ResetDownloadState`，与临安 ScreenInit 抢跑，条被重新武装。  
修正：撤销 pending 早投；unstick 仅 `outdoor-ready-stale-transfer-flags`。

## 验证

1. `make -j2`；重启 server。
2. 进梦境 → 使者出口 → 再进 → **再出口**（覆盖第二次出本）。
3. 期望：出本后**无** `pending_match` 早投；先 `scene_ready` 临安，再 `loading_clear remaining↓` / `dream_exit_settle_complete`；进度条消失可走。
4. 再点使者进本应能进（clear 尽后）；短暂「地图加载中」可接受。
5. 回归：户外地图石、梦境内商城进出。
