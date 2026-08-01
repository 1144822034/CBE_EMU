# 战斗后切图卡住（空 NPC 场景漏回 27/11）

```text
phase: battle return / local portal -> mmgame 25/5 followup -> 30/2 ResetDownloadState
trigger: 战斗结束后切到空 NPC 场景（实机日志：01桃花岛_02.sce @ (76,89)）
request: WT 25/5 len=44 = 25/5 + 6/1 + 6/13 + 6/14 + 2/10{Type=101} + 27/11
handler: builtin-mmgame-scene-transfer-followup
client parser: JianghuOL.CBE 0x01037998 (27/11), 0x01039770 -> 0x0103993C (30/2 ResetDownloadState)
```

## 已确认的偏离

服务端日志（卡住）：

```text
mmgame-transfer-followup target scene=01桃花岛_02.sce needsDownload=0 ... valid=1
mock_scene_npc_rearm ... npcnum=0 immediate=0
scene_ready ... reason=scene-target-complete
mock_mmgame_scene_transfer_followup ... objects=9 resp=591
net_send wt=25/5 len=44 source=builtin-mmgame-scene-transfer-followup
...
scene_sync_poll ... npc=1   # 过晚的空 27/11，不能闭合原 25/5 回调
```

首个协议偏差是：**`resources_ready=1` 且 `npcnum=0` 时，mmgame followup 完全不回 `27/11`**。响应仍含资源对象与无 `posinfo` 的 `30/2`，服务端已 `scene_ready`，但客户端复合请求里显式的 `27/11` 没有对应下行对象，合并的 `25/5` 回调无法结束，进度条卡住。

这与 `docs/re/2026-07-22-teleport-penglai-mijing-progress-stall.md` 同一契约：请求含 `27/11` 时必须回 `27/11`（允许空目录），不能因“没有 NPC 行可播”而省略对象。

## 为何打到 mmgame 路径

`vm_net_mock_is_mmgame_scene_transfer_followup_request` 在 pending scene target 有效时，匹配任意含 `25/5` 的包。战斗后本地切图没有先走会清 pending 的 `2/3` 完成包，因此 44 字节复合请求被 `builtin-mmgame-scene-transfer-followup` 抢先处理，进不了已修好的 `builtin-scene-task-subset-followup`（`mock_scene_task_subset_fb11_ack`）。

mmgame 路径本身仍负责本包末尾的 `30/2(no-posinfo)` 闭合，因此修复放在该 builder，而不是把整包改路由到 task-subset（后者不负责这次 `30/2`）。

## 排除项

- 不是缺 `30/2`：`objects=9` / autotest 已写 `resources+30/2-ack-no-posinfo`。
- 不是资源下载：`needsDownload=0`，`resources_ready=1`。
- 不靠 `scene_sync_poll` 补空 `27/11`：与 `docs/re/2026-07-24-scene-npc-return-investigation.md` 一致，poll 不是该回调的契约层。
- 本日志无 `mock_battle_drop_refresh` / `map_actor_vitals_sync`，不按掉落刷新或复活 vitals 劫持处理。

## 修复

`vm_net_mock_build_mmgame_scene_transfer_followup_response`：在 `resourcesReady` 时始终调用 `vm_net_mock_append_scene_npcs11_once_or_empty`，不再要求 `targetNpcCount > 0`。空场景回 `npcnum=0` 的 `27/11`，再跟资源对象与唯一的 `30/2(no-posinfo)`。

## 验证

- [x] `make -j2`
- [ ] 复测：战斗后进入 `01桃花岛_02.sce`（或其它空 NPC 图），进度条消失且可继续移动/交互
- [ ] 日志出现 `mock_scene_npc_rearm ... npcnum=0 immediate=1`，同包有 `mock_scene_npc_seed ... source=27/11-catalog npcnum=0`，且 `mock_mmgame_scene_transfer_followup` 的 object 数比修复前多 1
- [ ] 有 NPC 的场景（如 `c00蓬莱仙岛_01.sce`）仍在 followup 内下发非空 `27/11`，无重复节点、无第二 `30/2`
