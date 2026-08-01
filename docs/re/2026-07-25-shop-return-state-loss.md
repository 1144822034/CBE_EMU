# 进出商城后场景状态丢失

## 现象

玩家进入商城（mmShop）再退出后，新建的 mmGame 壳上多种状态看起来丢失：附近玩家、地图血蓝 HUD、组队血条、背包/商品列表错乱等。服务端角色持久化本身通常仍在。

## 业务链路

1. 商城打开：`1/1/14` / shop-info 等路径调用 `mark_shop_scene_npc_reseed_pending`，并置 `g_netMockShop17ListPending=1`、清 `g_netMockBackpackGridSeededRoleId`。
2. 客户端关闭 mmShop，重建 mmGame，再发 `WT6/1` 场景资源/任务请求。
3. 服务端以 `shopSceneNpcReseedPending` 识别商城返回，下发 `27/11`（可有）+ 资源/任务 + 无 `posinfo` 的 `30/2`，结束加载。
4. 随后依赖场景轮询 / group-type1 / 延迟 vitals 包恢复地图态。

## 预期 vs 实际

| 预期 | 实际（修复前） |
| --- | --- |
| 新 mmGame 再次收到附近玩家 baseline | `peerSync[].visible` 仍为进商城前的 true，scene-sync poll 认为无需 baseline |
| 背包 `17/1` 为角色物品 | `g_netMockShop17ListPending` 残留，下一次空 `17/1` 被商城目录劫持 |
| 组队血条可刷新 | 无 shop-return 侧 HSP 重投递 |
| WT6/1 不塞 login actorinfo | 保持（10 对象窗口；见 leader 商城崩溃文） |
| 仍可遇怪 | 见下方回归 |

## 根因陈述

- **触发**：任意进商城再退出（同场景 `shop-return` WT6/1）。
- **被违反的契约**：mmShop→mmGame 是新场景壳；观察者侧 nearby 游标、商城 `17/1` 一次性 pending 必须按“壳重建”重置，不能沿用进商城前的已投递状态。
- **首个错误状态**：`30/2` 完成后服务端仍认为 peers 已 visible、且 shop-list pending 仍武装；下一拍 poll/背包请求按“稳态刷新”处理。
- **证据**：`mock_server_social.c` scene-sync 仅在 `!peerSync->visible` 时 `needsBaseline`；`2026-06-26-npc-shop-purchase.md` 记录 shop `17/1` pending 劫持背包；`2026-07-25-leader-shop-buy-revival-crash.md` 禁止在 shop-return WT6/1 塞 actorinfo。
- **修改点**：`vm_mock_service_finish_shop_return_rehydrate` — 清 nearby 游标、清 shop17 pending、`scene_ready(shop-return)`、组队 HSP 双向重投；仍不向 WT6/1 注入 actorinfo。

### 回归：退出商城后穿过怪 / 不能遇怪（2026-07-25）

- **触发**：mmShop→mmGame 后踩怪穿过；进商城前 `request-live-node` 正常。
- **运行时路径（用户日志）**：退出商城走的是 `builtin-scene-task-subset-followup`（resp≈269），**不是** WT6/1 resource followup。此前只在 resource followup 上挂了完成对象。
- **首个偏离**：`01桃花岛_02` / `01桃花岛_04` 的 type-21 `npcnum=0`。`append_scene_npc_lifecycle_seed` 在 shop-return 且 `npcCount==0` 时若消费 `shopSceneNpcReseedPending`，既不发 `27/11` 也无完成对象。地图精灵仍在，kind-2 碰撞节点未重建；挂机仍可用缓存 `session-live-node`。
- **当时修复（已回退）**：同场景 `resources+30/1(当前坐标)` +
  `mark_direct_scene_enter_completed`。该路径引入二次 `EnterSceneByMapName`，
  导致 2026-07-27 loading 卡住（见 `2026-07-27-shop-return-loading-stall.md`）。
- **当前契约（2026-07-28）**：
  - shop-return follow-up：空图无非空 `27/11`；有 NPC 图 follow-up 只空
    `27/11`，shell poll `30/2` 后再 poll 非空目录（见
    `2026-07-28-shop-return-npc-catalog-defer.md`）。
  - **空 NPC 目录**（`npcnum=0`）：loading clear 全部结束（`remaining=0`）或
    `moveinfo-live` 提前取消 clear 后，延迟武装同坐标 `30/1`
    （`shop_return_kind2_reenter_arm` → poll/dispatch `shop_return_scene_enter`）。
  - **有 type-21 场景**：不武装 deferred `30/1`；非空 `27/11` 在 shell clear 后。
  - 禁止再把 `30/1` 塞进 shop-return 同包。

### 回归：能遇怪但二次加载清空 NPC（2026-07-25）

- **触发**：有 type-21 目录的场景（如 `c00蓬莱仙岛_01`）进出商城；第一次加载 NPC 可见，第二次加载后 NPC 消失，遇怪仍正常。
- **首个偏离**：当时的 shop-return `30/1` 会重建 kind-2，同时丢掉 type-21；随后若再强制二次加载且 follow-up 未及时补非空 `27/11`，NPC 永久缺失。
- **当前契约（2026-07-28）**：有 NPC 场景仍不发 shop-return / deferred `30/1`；同包或 follow-up `27/11` + poll `30/2` 完成壳重建。

## 验证

1. 进商城再退出：一次加载；日志
   `mock_scene_resource_followup_repeat_ack ... shop_return=1 completion=poll-30/2`
   或 `mock_shop_return_task_subset_complete ... completion=poll-30/2`；
   follow-up **无** 同包 `30/1-current-pos`。
2. 有 NPC 场景：同包或紧随 `scene_npc_lifecycle_seed ... shop-return-scene-followup-reseed npcnum>0`，NPC 仍在；日志可有
   `shop_return_kind2_reenter_skip ... reason=has-type21`。
3. **空 NPC 场景踩怪**：`shop_return_kind2_reenter_arm ... evidence=empty-npc-deferred-30/1`
   → `shop_return_scene_enter ... response=30/1` →
   `mock_challenge_battle_start ... target_source=request-live-node`。
4. 坐标保持退出前位置。
5. 战死进商城：仍跳过 shop-return 完成副作用，保留 `1/7/14`。
6. **退出不卡 loading（2026-07-27）：** 见
   `2026-07-27-shop-return-loading-stall.md`；期望 follow-up
   `completion=poll-30/2` + 可选 `mock_shop_return_loading_clear`；
   deferred `30/1` 仅在 clear 完成 / moveinfo-live **之后**。
