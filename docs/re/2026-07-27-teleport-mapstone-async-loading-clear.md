# 地图石 2/3 同包 30/2 早于异步 ScreenInit（野猪林）

Date: 2026-07-27

Status: implemented (server)

```text
phase: map-stone download=0 -> deferred 30/1 -> 2/3 current-scene-complete
       -> type27|WT6/1|task-subset (consume wait_wt6) -> poll delayed 30/2
trigger: 泰山地图石 -> 06野猪林_01.sce；桃花岛 -> 临安府_05
parsers: 30/1 -> 0x010396D6 EnterScene (async);
         30/2 no-posinfo -> 0x01039770 -> ResetDownloadState 0x0103993C
```

## 首个偏离

`27/12-ack`（无 posinfo）已生效（`resp=201`，日志
`27/12-ack+27-family+30/2-no-posinfo`），但客户端在 `201` **之后**仍出现
`ScreenInit caller=01018150`。同包末尾 `30/2` 与 WT6/1 的 `30/2` 仍清不掉
loading。

这与商城退出同源竞态（`docs/re/2026-07-27-shop-return-loading-stall.md`）：

1. deferred `30/1` 异步 `EnterSceneByMapName`
2. 紧随 `2/3` 同包 `30/2` 过早 `ResetDownloadState`
3. 之后才完成的 `ScreenInit` 重新挂上 DF_DataPackage，无收尾

`27/12` 去 posinfo 只去掉一条二次进图路径；它不能保证 `54` 的异步进图在
`201` 的 `30/2` 之前全部落地。

## 根因

地图石 `current_scene_completion` 在同包追加无坐标 `30/2`，违反
「`EnterSceneByMapName` 异步完成后再 `ResetDownloadState`」的契约。

## 修改

1. 地图石 `2/3`：保留 `27/12-ack + 空27/11 + 27/4 + 7/42`，**不再**同包挂
   `30/2`
2. `mapStoneLoadingClearPending`：在 `current_scene_complete` 后武装；poll 在
   `age_ticks>=2` 且无新 scene-transfer 时投递 lone `30/2` no-posinfo
3. 新 `scene_pending` 取消该 pending
4. WT6/1 的 `30/2` 兜底保留

## Follow-up 2026-07-27：仅 poll 清仍卡住（临安府_05）

```text
action=arm-poll-30/2-no-posinfo resp=149
WT6/1 completion=30/2 resp=334
mock_map_stone_loading_clear age_ticks=11 resp=59
# 客户端仍卡 loading；之后仍有 default-event / task_prompt poll
```

推断：download=0 地图石的 `2/3` **自身**仍需要末尾无坐标 `30/2` 闭合
scene-completion 回调；仅靠 poll/WT6/1 的 `ResetDownloadState` 不够。
同时异步 `ScreenInit` 仍需要 delayed poll 再清一次。

修正：

1. `2/3` 恢复同包 `30/2-no-posinfo`（契约闭合）
2. 保留 `map_stone_loading_clear` poll 延迟 `30/2`（覆盖晚到的 ScreenInit）
3. WT6/1 map-stone 路径改为 `completion=none`，避免第三次过早清

## Follow-up 2026-07-27：poll clear 抢在 wait_wt6 前（临安府_05）

```text
# 卡住
current_scene_complete ... arm-poll-30/2
mock_map_stone_loading_clear age_ticks=7   # wait_wt6 仍武装
type27-map-stone-wait-wt6 ... keep_loading_clear=1  # arm 已消费

# 桃花岛可走（对照）
current_scene_complete ... arm-poll-30/2
type27 ... clears wait_wt6
mock_map_stone_loading_clear age_ticks=10
```

首个偏离：poll lone `30/2` 在 type27/WT6/task-subset 消费 `wait_wt6` **之前**
投递。过早 `ResetDownloadState` 后晚到的 `27/12+posinfo` ScreenInit 再挂
loading，而 clear arm 已空。

修正：`map_stone_loading_clear` 在 `wait_wt6` 仍为 true 时 hold（不消费
pending）；`wait_wt6` 清掉后再按 `age_ticks>=2` 投递。`age_ticks>=15` 超时
仍允许清条（与 NPC wait_wt6 poll fallback 同量级），日志
`reason=wait_wt6-timeout`。

与 shop-return 无关：商城返回已恢复同包 `27/11+30/2-no-posinfo`，无
`shopReturnLoadingClear*`。

## Follow-up 2026-07-27：type27 同包 30/2 仍竞态（蓬莱）

```text
# hold wait_wt6 已生效，顺序正确，但仍卡 loading
current_scene_complete ... arm-poll-30/2
type27 ... completion=30/2-no-posinfo keep_loading_clear=1
mock_map_stone_loading_clear age_ticks=18
# 客户端仍卡；随后仍能开商城
```

首个偏离：wait_wt6 follow-up（type27）在同包末尾再挂无坐标 `30/2`，与
`27/12+posinfo` 二次 `EnterScene`/`ScreenInit` 竞态，和过早 poll clear 同源。
随后单次 poll clear 无法可靠覆盖晚到的 ScreenInit。

修正：

1. type27 / task-subset / WT6/1 wait_wt6：`completion=none`（不挂 trailing 30/2）
2. 投递后 `map_stone_loading_clear_rearm` 重置 age 时钟
3. poll multi-shot（remaining=3，每次清后 gap `age_ticks>=2`）覆盖晚到 ScreenInit
4. 仍 hold 至 `wait_wt6` 消费（或 `age>=15` 超时）

## 验证

1. 停掉占用中的 `jh-online-server` 后 `make -j2`；重启服务
2. 复测：临安 → 蓬莱 / 桃花岛 → 临安府_05；野猪林 / 泰山地图石
3. 期望服务端顺序：
   - `mock_teleport_stone_current_scene_complete ... action=reset-download-state+arm-poll-30/2`
   - `type27` / `WT6/1` / `task-subset` seed_deliver **`completion=none`** +
     `map_stone_loading_clear_rearm`
   - **之后**多次 `mock_map_stone_loading_clear ... remaining=N`（无 clear 早于 type27）
4. 期望客户端：loading 消失、玩家精灵可见、可移动
5. 回归：进出商城为同包 `completion=30/2-no-posinfo`；**无**
   `mock_shop_return_loading_clear` / `30/1-current-pos`
