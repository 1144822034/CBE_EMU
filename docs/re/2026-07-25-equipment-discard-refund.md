# 2026-07-25 装备丢弃补偿铜钱

## 需求

装备丢弃时，按装备目录价值的十分之一补偿铜钱。

## 契约位置

丢弃请求仍是既有 `WT 7/4`；处理函数
`vm_net_mock_build_item_discard_response`。装备价值取自商店/装备目录加载的
`equip.dsh` `价值` 列（与 NPC 购买价同源）。

## 修改

1. 成功消费背包行且该 `itemId` 属于装备目录时：
   - `refund = floor(price / 10) * discardCount`
   - `role->money += refund`（封顶 `u32`）
2. 持久化仍走 `item-discard`。
3. `refund != 0` 时在原有 `7/4+17/1+7/42+7/11` 后追加 `1/10/26`
   `{result,type=1,npcnum,name,money=<新余额>}`（与 group/type-1 铜钱字段同源），
   即时刷新背包界面铜钱。不可用 `1/1/14`（背包丢弃路径不消费）或 `7/26`
   （会打开任务大厅）。
4. 普通道具丢弃不补偿。

## 验证

- `make -j2`
- 丢弃一件已知价值装备（例如价值 675 的白装），日志
  `mock_item_discard ... refund=67 ... refresh=...+10/26`，背包铜钱立即 +67。
