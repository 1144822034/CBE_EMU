# 交易装备强化等级异常

## 触发条件

1. 甲角色背包中有一件装备实例，强化等级为 `0`。
2. 乙角色持有相同物品 ID 的装备，或曾持有过该装备实例。
3. 甲通过交易窗口将该装备交给乙。

此前乙收到的装备可能显示为一个与甲不一致的强化等级；其中可稳定复现的一种情况是交易结算复用了乙原有同 ID 行的强化状态。

## 完整链路与证据

客户端的交易请求 `WT21/5` 只提交背包序号和数量。服务端必须在报价校验阶段从权威背包行补全实例属性：

```
WT21/5 请求 (sourceSeq,count)
  -> vm_net_mock_trade_validate_offer
  -> 会话报价 vm_mock_service_trade_item
  -> WT21/6 对方报价预览
  -> WT21/7 双方确认
  -> vm_mock_service_trade_commit
  -> MySQL account_role_backpack
  -> WT21/8 收货回执 (destinationSeq,itemId,count)
```

`tmp/ida_full_jh_actor_update/decompiled.c` 中 `HandleShopBuyItem`（`WT21/6`）在每个报价行的 `itemId/count` 后调用 `ParseEquipAttributes`。该共同扩展的前两个 `i16` 依次是当前强化等级和强化上限；因此 `WT21/6` 必须在第一个 `i16` 写入来源装备实例的真实 `enhanceLevel`，并在第二个 `i16` 写入该装备的上限。

同一反编译文件中 `BuildShopBuyList`（`WT21/8`）每行仅读取 `destinationSeq`、`itemId`、`count`。该回执不能附加装备扩展字段；接收端必须以新背包实例序号在后续背包刷新中获得状态。

## 首次偏离与根因

`vm_mock_service_trade_item` 原先仅保存 `itemId/sourceSeq/destinationSeq/count`，在 `vm_net_mock_trade_validate_offer` 已经定位到来源背包行后仍丢弃了 `enhanceLevel`。

随后 `vm_mock_service_trade_role_add_item` 对所有物品按 `itemId` 合并。装备被合并到接收方已有同 ID 行时，该行既有的 `enhanceLevel` 和 `seq` 被保留，而来源装备的实例身份已经丢失。这是第一处违反“装备按背包实例归属”的契约；MySQL 持久化只是正确保存了这个已经错误的接收方行。

已排除的方案：

- 不向 `WT21/8` 增加额外属性字段：客户端只读取三项，改变该包会破坏 parser 边界。
- 不在客户端按物品 ID 清零强化等级：这会掩盖服务端实例归属错误，也会错误处理真实强化装备。

## 修复

- 报价状态增加 `enhanceLevel`，校验时从来源背包行复制并限制为最大等级。
- `WT21/6` 的 item-common-extra 写入报价装备的真实强化等级，供对方交易预览解析。
- 交易结算将完整报价行传给收货逻辑。
- 装备由装备目录判定，必须数量为 `1`，且永不按 item ID 合并；收货方创建新序号的新背包实例，并复制来源强化等级。
- 普通可堆叠物品保留既有按 item ID 合并逻辑。
- 每个结算行写入 `trade_item_transfer` 取证日志，记录来源/目的角色、序号、数量、强化等级和装备判定。

## 验证目标

- 将强化 `0` 的装备交易给已有相同 item ID、强化非零装备的角色：接收方保留两条不同序号，收货的新行强化为 `0`。
- 将已强化装备交易：`WT21/6` 预览和接收方新行均为该真实强化等级。
- 交易普通可堆叠消耗品：仍合并到既有行并返回其 destination sequence。
- 双方确认后中断或持久化失败：MySQL 事务不提交半边交易。

定向回归夹具：`tmp/trade-equipment-instance-regression.c`。它直接验证上述私有结算 helper 的实例分离和 `WT21/6` 强化字段，不依赖已与当前登录前置流程失配的旧网络夹具。可从项目根目录编译运行：

```powershell
gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 -ffunction-sections -fdata-sections -w `
  tmp/trade-equipment-instance-regression.c src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c `
  -Wl,--gc-sections -o tmp/trade-equipment-instance-regression.exe `
  -lpthread -liconv -lm -lkernel32 -lws2_32
.\tmp\trade-equipment-instance-regression.exe
```

历史上已错误合并到同 ID 旧行的装备无法从现有关系表可靠地区分“原物”和“交易物”，因此本修复不做猜测性数据迁移；它保证此后每一笔装备交易保留来源实例状态。
