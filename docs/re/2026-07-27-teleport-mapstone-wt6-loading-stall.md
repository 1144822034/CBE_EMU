# 地图石传送卡 loading（临安府 WT6/1 completion=none）

Date: 2026-07-27

Status: implemented (server)

```text
phase: map-stone download=0 -> deferred 30/1 -> 2/3 current-scene-complete -> type27 hold -> WT6/1 NPC seed
trigger: 桃花岛地图石 -> c04临安府_05.sce (download=0)
server: current_scene_complete resp=221 (27-family+30/2); type27_hold; seed_deliver WT6/1 resp=284 completion=none
client: DoLoading; queue 221; EnterSceneByMapName caller=01018150 ScreenInit; queue 116/284/poll; stuck loading
```

## 首个偏离

服务端地图石契约本身按预期走完：

1. `mock_teleport_stone_deferred_enter`（poll `30/1`）
2. `mock_teleport_stone_current_scene_complete ... 27-family+30/2-no-posinfo`
3. `mock_scene_npc_type27_hold ... wait-wt6`
4. `mock_scene_npc_seed_deliver ... WT6/1` 且 `npcnum=1`

但客户端在收到 `resp=221` **之后**又执行了一次
`EnterSceneByMapName(0x01018150)`（`ScreenInit`），重新挂上 loading。
随后 WT6/1 日志为：

```text
mock_scene_resource_followup_repeat_ack ... completion=none
```

即 NPC 种子包**没有**再带无坐标 `30/2`。`0x01039770` → `ResetDownloadState`
`0x0103993C` 不会再跑，进度条/加载壳卡住。

这不是 HAS_FOLLOWUP / 仓库改动：本次传送相关 CBMR 均为 `flags=0 followup=0`。

## 根因

`tongquetaiNpcSeedAfterCurrentCompletion`（实为全体 download=0 地图石的
`wait_wt6` NPC 投递）只补 `27/11`，`needCompletion30_2` 仅对
`shopReturnReload || currentSceneReload` 为真。

地图石 `2/3` 上的那次 `30/2` 不足以覆盖「221 之后又一次 EnterSceneByMapName」
的二次 loading；WT6/1 作为该路径上客户端随后必发的 scene-runtime 请求，应在
投递非空 `27/11` 后追加无坐标 `30/2` 收尾。

## 修改

`mock_server_interaction_login.c`：当 `tongquetaiNpcSeedAfterCurrentCompletion`
时 `needCompletion30_2=1`，使 WT6/1 日志变为
`completion=30/2-no-posinfo`。

## 验证

1. `make -j2`；重启 mock-service
2. 复测：桃花岛 → 临安府地图石
3. 期望：`mock_scene_npc_seed_deliver ... completion=30/2-no-posinfo`，
   `repeat_ack ... completion=30/2-no-posinfo`；客户端 loading 消失进图
4. 回归：铜雀台 / 无情谷 download=0 地图石同样应带该收尾；真下载路径仍由
   `defer-30/2-until-WT6/1` 单独管，不走本分支

## Follow-up 2026-07-27：二次 Enter 的根因

WT6/1 的 `30/2` 只是兜底。客户端在 `2/3` 后再次 `ScreenInit caller=01018150`
是因为同包 `27/12 name+posinfo` 经 `0x0100E9B8` 再进图。根因修复见
`2026-07-27-teleport-mapstone-2712-reenter.md`（地图石 `27/12` 改为无 posinfo ack）。

## Follow-up 2026-07-27：同包 30/2 仍过早

`27/12-ack` 后 `resp=201` 仍见 `ScreenInit caller=01018150`，同包/WT6/1 的
`30/2` 清不掉 loading。与商城退出相同：异步 `EnterScene` 晚于同包
`ResetDownloadState`。改为 poll 延迟 lone `30/2`，见
`2026-07-27-teleport-mapstone-async-loading-clear.md`。
