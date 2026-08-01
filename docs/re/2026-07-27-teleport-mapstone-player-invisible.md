# 地图石进图后场景可见但玩家不显示

Date: 2026-07-27

Status: implemented (server) — `27/12+posinfo` 绑精灵（见 double-load 文档）

```text
phase: map-stone deferred 30/1 -> 2/3 (27/12+posinfo) -> delayed 30/2 -> type27 27/11
```

## 根因

地图石 deferred `30/1` 重建 mmGame 壳后，本地行走精灵不会自动回到新壳。
传送门 post-enter 用 `27/12 name+posinfo`（`0x0100E9B8`）绑定；地图石必须同族。

`27/12-ack` + scene-poll `1/1/6`（`resp=440`）已证伪：NPC 有、人没有。

## 修改

恢复 `27/12+posinfo`；二次 EnterScene 用同包/poll `30/2` 清 loading。
详情：`docs/re/2026-07-27-teleport-mapstone-double-load.md`。
