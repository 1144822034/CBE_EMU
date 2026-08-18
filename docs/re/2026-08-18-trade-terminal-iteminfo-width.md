# 交易结算装备属性错位：WT21/8 数量字段宽度

## 触发与现象

未强化的梦境装备与另一件物品一起交易后，服务端日志中的来源实例和接收方持久化实例均为
`enhance=0`，但接收方客户端把梦境装备显示为 `+16`。

## 业务链路与第一次偏离

交易链路为：

```text
WT21/5 报价
 -> vm_net_mock_trade_validate_offer
 -> WT21/6 对方预览
 -> WT21/7 双方确认
 -> vm_mock_service_trade_commit
 -> WT21/8 接收方结算
 -> JianghuOL.CBE:BuildShopBuyList(0x010259B6)
```

运行日志证明 `vm_mock_service_trade_commit` 在转移 `40085` 和 `6404` 时创建的接收方实例
仍为 `enhance=0`，因此持久化不是首次偏离。

IDA 中 `BuildShopBuyList(0x010259B6)` 对每条 `iteminfo` 依次调用：

```text
reader + 0x24: read_i16(destinationSeq)
reader + 0x20: read_i32(itemId)
reader + 0x20: read_i32(count)
```

旧 builder 却把结算行写为 `i16 destinationSeq + i32 itemId + i16 count`。两件物品时，
客户端读取第一行 `count` 会再吞掉第二行 `destinationSeq` 的两个字节，随后第二行的序号、
物品 ID 和数量全部错位。客户端从错误边界创建本地装备对象，异常的 `+16` 是该错位后的
表现，不是服务端权威强化值。

## 修复

`vm_net_mock_append_trade_terminal_object` 现在严格写入：

```text
i16 destinationSeq
i32 itemId
i32 count
```

没有向 `WT21/8` 追加强化扩展，也没有补发 `17/1`、`30/21` 或 `7/7`。这些对象分别属于
详情缓存、网格初始化或新增实例生命周期，不能用来覆盖已经由交易结算创建的同一实例。

## 回归边界

`scripts/trade-terminal-iteminfo-regression.c` 构造两条不同序号、物品 ID 和数量的接收行，
调用真实 `WT21/8` builder，并按固件相同的 `i16/i32/i32` 顺序逐字节读取。测试要求两行
完整解析且游标精确到达 32 字节末尾，以防第一行再次吞掉第二行。

