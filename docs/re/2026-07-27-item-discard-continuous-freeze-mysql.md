# 连续丢装备再次卡住：同步 MySQL 拖死 7/4

日期：2026-07-27

## 触发与现象

公网玩家连续丢弃装备约 5–6 次后界面卡住，需重进。商城等短 `data`
请求仍可正常。PC/内网较轻。

## 根因

`JianghuOL.CBE:0x01033544` 仅在收到 `7/4` 时清除物品操作 waiting flag。

每次装备丢弃在 `vm_net_mock_build_item_discard_response` 内同步调用
`role_db_save`：对 `account_role_backpack` 做整表 DELETE+INSERT，且占用
全局 protocol lock。公网 MySQL 延迟下 `process_ms` 被拉长，客户端
`data_request` 超时收不到 `7/4` → waiting flag 永不清除。

此前「follow-up 编码失败 return 0」已修（见
`2026-07-27-item-discard-freeze-and-bind.md`），但同步落库仍可在连续丢时
重现卡死。

## 修改

1. **丢弃延迟落库：** 成功消耗后置 `g_vm_net_mock_role_inventory_dirty`，
   不在组包路径上 `role_db_save`。CBMR 发出后再 flush
   （`item-discard-deferred`）；断线/会话下线一并 flush。
2. **`7/4` followup 拆包：** 传输层把 `17/1+7/42+7/11+10/26` 拆成第二次
   `queue_data`，主事件只留 `7/4` 先清 wait（需更新 Android
   `network-client.c`）。

## 验证

1. `make -j2`，部署新服务端。
2. 连续丢 10+ 件带补偿装备：每次 `mock_item_discard` 的 `process_ms` 应明显
   低于原先同步落库；随后有 `item_discard_deferred_flush`。
3. 客户端不应再卡死；可选装含 peel 的新 APK 以进一步降低同事件解析负担。

## 风险

进程在 CBMR 已发、deferred flush 之前崩溃时，最近若干次丢弃可能未写入
MySQL（会话内存已丢）。断线路径会尽量补写。
