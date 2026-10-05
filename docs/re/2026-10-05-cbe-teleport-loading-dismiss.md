# CBE 世界地图传送卡在加载界面

Date: 2026-10-05

Status: implemented

## 1. 当前卡点

- 可见现象：临安府世界地图确认传送到桃花岛后，目标图 `ScreenInit Ok`，仍停在加载，直到关窗口。
- 触发：`bin/client_out.txt`，本机 `127.0.0.1:19090`。
- 路径：`1/16/4` → `1/16/2+1/16/3+1/7/1` → 延迟 `WT30/1` → DF `ScreenInit` → `1/2/3+1/27/11+1/7/42`。

## 2. 运行时证据

```
remote_scene_target_apply … scene=01桃花岛_01.sce … allow_map_stone_reenter=1
download_busy_clear reason=do-loading-immediate …
ScreenInit Ok
cbe_uplink len=72 hdr=1/2/3 objects=1/2/3,1/27/11,1/7/42
queue_data … resp=229
host quit cleanup … depth=3 current=01053f78
```

没有 `map_stone_dismiss_bar_arm`，也没有 `download_busy_clear reason=map-stone-world-teleport-screeninit`。

## 3. 根因

室外 `WT30/1` 在 `vm_net_mock_apply_remote_observation` 里把 `g_vm_map_stone_dismiss_bar=1`、`tick=0`，意图是 DF `ScreenInit` 后再延迟 2 tick 调 `ResetDownloadState`（`0x0103993C`）。

`2/3` 里的 `27/12+posinfo` 会再走一次 `EnterSceneByMapName`，可能在同包 `30/2` 之后重新武装 `DoLoading`（见 `mock_server_social.c` 地图石 completion 注释）。

实际 DF `ScreenInit` 分支把 `dismiss_bar` **清零**，pump 要求 `tick!=0`，延迟关闭从未触发。进度条被后续 `2/3` 抬起后没有对应的 `30/2`/host clear。

## 4. 修改

- `src/main.c` 与 JNI 副本：DF `ScreenInit` 成功且已武装时设 `tick = schedulerTick+2`，不再取消武装。
- 到期仍走已有 `vm_host_pump_map_stone_dismiss_bar` → `download_busy_clear reason=map-stone-world-teleport-screeninit`。

未改服务端包结构。`2/3` 仍应尽量带无坐标 `30/2`；本修复只补客户端在二次 EnterScene 之后的关闭契约。

## 续记：挂机 vitals 抢走 2/3（2026-10-05）

客户端 dismiss 已触发后仍卡「正在加载中」：

```
resource_cache_probe_unusable … 临安府_05.sce resp=226 kind=1/14
ScreenInit Ok
map_stone_dismiss_bar_arm / download_busy_clear map-stone-world-teleport-screeninit
cbe_uplink len=73 hdr=1/2/3 objects=1/2/3,1/27/11,1/7/42
cbe_actorinfo_cap object=1/1/14
```

无进图 `queue_data`。先挂机使 `pendingMapActorVitalsSync remaining=2`：第一次顶掉 WT18/7，第二次把 `2/3` 答成 `1/1/14`（`builtin-map-actor-vitals-sync resp=226`），没有 `30/2`。

修改：`mock_server_dispatch.c` 的 wt-dispatch 不再用 vitals 抢 `18/*`、`16/*`、`2/3`。血量同步仍走 scene-poll。

验证：挂机后世界地图切图；`18/7` 不应再是 `kind=1/14`；`2/3` 应走 current-scene-ack 并带 `30/2`。需重启 `jh-online-server`。

## 续记：梦境出本卡住（2026-10-05）

`29*` NPC `26/1` 出本 → `30/1` 临安府_06。资源 `uptodate`，`ScreenInit init=0x051c83e5`。武装了 `dismiss_bar`，但判定用 `0xfff00000==0x05000000`，`0x051c` 没命中，金条不关。`2/3` 二次 ScreenInit 后再关窗口仍 `depth=3`。

金条是 DF 在 `download state=2` 时画的覆盖层，不是必须单独进的场景。本地资源已齐时：DF `ScreenInit` 立刻 `ResetDownloadState`（不画条），两拍后再清一次防 `2/3` 重武装。进图 ScreenInit 本身不跳过。

判定改为 `0x05xxxxxx`（`vm_host_screen_init_is_loaded_df`）。

## 续记：NPC 店返回仍卡金条（2026-10-05）

`bin/client_out.txt`：当铺/NPC 壳 `init=0x05017c09` 走 `1/1/14` 后 `mmshop_loading_clear skip … not-mmshop`，随后 mmGame `DoLoading` + `do-loading-immediate`，`ScreenInit init=0x05187c45` 临安府_05 `uptodate`。关窗口仍 `depth=3`。中间 poll `resp=55/325/11`，无 `2/3`、无 `30/2`、无 `map_stone_dismiss_bar_arm`。

`skip_bar` 只在真正 mmShop / 世界地图 `WT30/1` 预武装。NPC 店返回、`5/10` 再水合会把 `state=2` 重新抬起，host 不再关条。

修改：任意已加载 DF `ScreenInit Ok`（`0x05xxxxxx`）都立刻 `ResetDownloadState` 并武装两拍后再清；仍调用 `DoLoading`，不跳过进图 Init。

## 5. 验证

1. 临安府世界地图传到桃花岛：`ScreenInit Ok` 后应有 `map_stone_dismiss_bar_arm`，约两拍后 `download_busy_clear reason=map-stone-world-teleport-screeninit`，地图可走。
2. NPC 店 / 当铺返回：`ScreenInit Ok` 后应有 `loaded-df-screeninit-skip-bar` 与 `map_stone_dismiss_bar_arm`，金条落下，地图可走。
3. 同图地图石 / 需 WT18/7 下载的切图不应在 ScreenInit 之前因该 clear 提前丢进度。
4. `make -j2`。
