# CBE 模拟器：开商城无物品卡住 / 传送卡住

Date: 2026-10-04

Status: implemented

## 1. 当前卡点

- 可见现象：CBE 模拟器连怀旧服，打开商城不显示物品并停在加载；传送石切图后停在目标场景加载。
- 触发方式：`bin/client_out.txt`，`106.54.59.92:19097`。
- 本轮最小目标：只改模拟器，不改服务端。

## 2. 运行时证据

商城：

```
cbe_uplink len=61 hdr=1/2/10 objects=1/2/10,1/14/14,1/14/4,1/14/5,1/14/6
swallow_hangup_uplink len=61 evidence=bag-equip-detail-clickthrough
empty_wt_ack connect=2 event=7 len=5 objects=0
```

mmShop 开店上行被当成挂机开战吞掉，本地空 WT ack，秘宝页 `14/5` 从未到达客户端。

传送：

```
remote_scene_target_apply … scene=c04临安府_04.sce …
resource_cache_version_check file=c04临安府_04.sce … exists=1
resource_cache_download_failed file=c04临安府_04.sce received=0 total=0 exists=1
```

中间没有 `data_request` 重试日志：WT18/7 第一次交换“成功”但解析不成 `result=1` 的 18/7，探测失败。随后 `1/2/3` 仍发出，scene-poll 连续 connect 失败。

## 3. 根因

1. `vm_client_request_is_cbe_hangup_start` 只要包里有 `2/10` 且整包出现 `Type=2` 就判挂机。开店包把挂机 companion 和 `14/14+14/4+14/5+14/6` 打在同一 WT。`stack>=2` 时 `vm_host_should_block_hangup_uplink` 为真，整包被吞。
2. 场景 `.sce` 在 `fopen` 路径上同步探 WT18/7。探测失败时函数返回 false；本地文件虽仍能打开，但失败路径不记 keep-local，且异常形状响应可能污染复用的 data session。

## 4. 修改（仅模拟器）

- `src/network-client.c` 与 JNI 副本：挂机开战只认 `2/10 Type=2` + 可选 `25/3` / `2/1`，其它对象（含商城 `14/*`）一律转发。
- `src/main.c` 与 JNI 副本：18/7 探测不可用时打 `resource_cache_probe_unusable`；非 18/7 形状则断开 data session 最多再探 2 次；本地已有文件则 `resource_cache_keep_local`。

未改 `src/mock-server.c` / `web_admin_server.c`。

## 5. 验证

1. 野外进江湖商城：应出现 `cbe_uplink … 14/14,14/4,14/5,14/6`，**没有** `swallow_hangup_uplink`，秘宝页有物品。
2. 背包点装备详细：若仍组纯 `2/10 Type=2`，仍应 `swallow_hangup_uplink`，不开挂机。
3. 传送石切图：目标 `.sce` 本地存在时即使探测失败也应 `keep_local` 并完成进图，不应再只打 `download_failed received=0`。

## 续记：世界地图传送进度条（2026-10-04）

重登已在目标图 → 服已落 `30/1`。卡住的是 CBE `DoLoading` 等无坐标 `30/2`（`ResetDownloadState` 0x0103993C）；怀旧服 `2/3` 常不带 `30/2`，且 `2/3` 投递会把 queued `downloadSnapshot state=2` 写回，进度条再抬起来。

客户端（不改服）在室外 `30/1` 后：DF `ScreenInit Ok` 延迟 2 tick 调已有 `vm_host_clear_download_busy`；此期间禁止恢复 state=2 快照。日志：`download_busy_clear reason=map-stone-world-teleport-screeninit`。

2026-10-05：`ScreenInit` 曾把 `dismiss_bar` 清零，延迟关闭从未触发。见 `2026-10-05-cbe-teleport-loading-dismiss.md`。
