# 有 NPC 无野怪图：出店场景闪现后再卡进度条

日期：2026-08-01

## 现象

有 type-21 NPC、无野怪的地图退出商城：场景先显示一次，随后进度条再起并卡住。

## 业务链路

```text
mmShop exit (npcnum>0)
  → shell poll 30/2 ×N     ← 第一次 ResetDownloadState，场景闪现
  → poll 非空 27/11        ← 第二次 DoLoading（NPC 资源）
  → post-catalog poll 30/2 ← 必须关掉第二次 loading
  → busy 26/0
```

## 根因陈述

- **触发**：有 NPC 目录的场景退商城（站立亦可）。
- **被违反的契约**：deferred `27/11` 打开的第二次 DoLoading 必须由 post-catalog
  `30/2` 关闭；不能在投递窗口用错误的场景权威取消 clear/catalog。
- **首个错误状态**（择一或叠加）：
  1. post-catalog / catalog 用 `current_scene_name()`（role/host）与
     `shopReturn*Scene`（连接 `sceneVisibleScene`）比对 → 误 `scene-mismatch`
     **cancel**，第二次 loading 无 `30/2`。
  2. catalog 在 stale `direct/deferred/target_valid` 上 **永久 cancel**
     （shell 已清条），后续无 catalog+post-catalog 闭环。
- **证据**：地图石 clear 已改用 `client_visible_scene_name`
  （`2026-08-01` 临安→桃花岛 type27 串目录）；shop-return 仍用
  `current_scene_name`。用户 warn 里的 `not-published` 多为并发进图资源缺口，
  不是本 hold/cancel 的充要条件。
- **排除**：空图 kind-2 路径（本场景 npcnum>0 走 catalog）；本次落盘改动不改包序。

## 修改

1. shop-return loading clear / NPC catalog：场景比对改
   `vm_net_mock_client_visible_scene_name()`。
2. catalog：`sceneVisiblePending` 或非豁免 liveTeleport 时 **hold**（return 0），
   不再 `cancel ... scene-transfer-active`。
3. post-catalog mismatch：**hold** 不清 pending；仅 shell clear 可 cancel。

## 验证

1. `make -j2`，重启。
2. 临安/蓬莱等有 NPC 无野怪图：进商城再退出（站立）：
   - `catalog_deliver ... post_catalog=1`
   - `loading_clear ... post_catalog=1` remaining→0
   - 无 `loading_clear_mismatch ... action=cancel`（post_catalog）
   - 无长期二次进度条；可走；NPC 可见
3. 空图退商城仍可 kind-2。
