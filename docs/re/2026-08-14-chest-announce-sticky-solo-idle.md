# 开箱玄晶世界播报粘住（单人 idle poll）

日期：2026-08-14

## 触发与现象

玩家开出一次五级+玄晶并出现世界祝贺后，之后再开箱常会再播**同级玄晶**祝贺，而本次 tip/背包并没有开到该玄晶。

## 业务链路

1. 单次开箱 `1/7/15` 成功且奖励达稀有门槛 → `maybe_announce_rare_reward` 把 `chestAnnounce*` 记在会话上，`flush_tick = now+5`（约 500ms）。
2. 到期 flush 应在 `scene_sync_poll` → `append_scene_sync_chat_objects*` 里执行，再 fanout `1/3/3` type0。
3. 十连 `1/7/24` 走 `announce_rare_reward_count` **立即** flush，不受本 bug 影响。

## 首次偏离

`build_scene_sync_poll_response` 的 **solo idle 快路径**在「无 NPC/聊天/社交/邻人」时直接 `return 0`，**未检查** `chestAnnounceCount`。

单人地图上绝大多数 poll 走该快路径 → pending 玄晶祝贺**永远到不了** chat append 的 flush。

下次开到**另一种**稀有（紫装 / 别级玄晶 / 经验卡等）时，`maybe_announce` 会先 `flush_pending` 旧玄晶再武装新稀有 → 世界频道在「本次没开到该玄晶」的开箱上播出旧同级玄晶。pending 再次粘住，可反复出现。

## 根因

稀有祝贺的 debounce 投递依赖 scene poll；idle 快路径把「仅有 chestAnnounce pending」当成空闲并跳过，违反了 coalesce 契约。

（若背包堆叠 UI 仍显示 1，会加重「没开到」观感；那是 `mergeBag17WithGridSeq` / 拍卖领取另一条线，见 `2026-08-14-auction-claim-crystal-stack.md`。）

## 修改

| 文件 | 内容 |
| --- | --- |
| `mock_server_social.c` | idle 快路径增加 `chestAnnounceCount == 0`，有 pending 时落入 chat append 并按 `flush_tick` 广播 |
| `mock_server_equipment_npc.c` / `mock-server.c` | `announce_flush_if_due`：任意成功 `7/15` 后若 pending 已到期则立刻入队 |
| `mock_server_catalog.c` | 单次开箱成功路径调用 `flush_if_due`（含非稀有） |

## 验证

1. 单人地图：开出一次 905–916 → 约 0.5s 内应出现一条 `[世]`；日志 `mock_chest_open_world_announce_pending` 后跟 `mock_chest_open_world_announce`（不必再开一次稀有才播）。
2. 单人：先开玄晶，再开非稀有若干次 → 不应在非稀有开箱上再播该玄晶；仅应在首次 pending 到期播一次。
3. 单人：先开玄晶 A，500ms 内再开稀有 B → 可先 flush A 再 pending B（契约允许）；A 不应拖到很久以后才播。
4. `make -j2`
