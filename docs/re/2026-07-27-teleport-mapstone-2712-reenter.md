# 地图石 2/3 的 27/12+posinfo 二次进图卡住（野猪林）

Date: 2026-07-27

Status: implemented (server)

```text
phase: map-stone download=0 -> deferred 30/1 -> 2/3 current-scene-complete -> type27 hold -> WT6/1
trigger: 地图石 -> 野猪林（及同类 download=0 目标）
client: queue_scene_poll resp=54; ScreenInit caller=01018150;
        queue_data resp=219; ScreenInit caller=01018150 AGAIN;
        queue_data resp=116,265(completion=30/2),23
parsers: 30/1 -> 0x010396D6 EnterScene; 27/12 name+posinfo -> 0x0100E9B8 -> 0x01018150;
         30/2 no-posinfo -> 0x01039770 -> ResetDownloadState 0x0103993C
```

## 首个偏离

服务端协议链在 野猪林上已走完（deferred enter → current_scene_complete+30/2 →
type27 hold → resource-followup `completion=30/2`）。卡 loading 的首个客户端
偏离是：

1. poll `resp=54`（deferred `30/1`）已 `EnterSceneByMapName` / `ScreenInit`
2. `2/3` `resp=219`（27-family + 末尾 `30/2` no-posinfo）之后**再次**
   `ScreenInit caller=01018150`
3. 同包末尾的 `30/2` 与后续 WT6/1 的 `30/2` 都只是在二次进图之后清 loading；
   正确契约应避免这次二次进图

## 根因

`ui_apply_named_posinfo_target`（`JianghuOL.CBE:0x0100E9B8`）对
`27/12 name+posinfo` 也会进场景进入 vtable，不是无进图坐标刷新
（见 `docs/re/2026-07-16-scene-transfer-single-load.md`）。

地图石路径上 deferred `30/1` 已是唯一允许的带坐标进图对象；
`current_scene_completion` 仍调用 `append_fb_target_result12_for_scene` 带
`posinfo`，违反「deferred completion 只保留一个 position-bearing 对象」。

此前补丁（`2026-07-27-teleport-mapstone-wt6-loading-stall.md`）在 WT6/1 再挂
`30/2` 只是兜底清 loading，未修二次 `EnterScene` 本身。

## 修改

- `vm_net_mock_append_fb_target_result12_ack_for_scene`：`27/12` 仅
  `result+fb+name`，无 `posinfo`
- `vm_net_mock_build_current_scene_completion_response`：当
  `closeTeleportDirectEnter` 时用 ack 形态；非地图石路径仍带 `posinfo`
- WT6/1 地图石 `30/2` 兜底保留

## 验证

1. `make -j2`；重启 mock-service
2. 复测：地图石 → 野猪林 / 临安府 / 铜雀台（download=0）
3. 期望服务端：
   `mock_teleport_stone_current_scene_complete ... response=27/12-ack+27-family+30/2-no-posinfo`
4. 期望客户端：`resp=`（2/3）之后**不应**再出现 `caller=01018150` 的
   `ScreenInit`；loading 应由同包或 WT6/1 的 `30/2` 清掉
5. 回归：非地图石 current-scene / portal 仍用带 `posinfo` 的 `27/12`

## Follow-up 2026-07-27：ack 后仍 ScreenInit

`27/12-ack` 使 `resp=219→201`，但 `201` 后仍有 `caller=01018150` 的
`ScreenInit`。同包 `30/2` 相对异步进图过早，见
`2026-07-27-teleport-mapstone-async-loading-clear.md`（改为 poll 延迟清 loading）。
