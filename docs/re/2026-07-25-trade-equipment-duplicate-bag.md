# 交易获得装备在客户端出现多份（服务端仅一份）

日期：2026-07-25

状态：根因已确认并修复

## 触发条件

双方完成装备交易（日志可见 `trade_commit` + 二次确认 `21/8 commit=1`）。
接收方打开背包，同一强化等级的装备显示为多格相同的 `(+N)` 物品；
`account_role_backpack` / 内存角色背包仍只有一行。

## 证据与链路

1. 成功结算对接收方下发 `21/8 { result, num, money, iteminfo }`，由
   `0x01027726 -> BuildShopBuyList(0x010259B6)` 消费；该路径与任务
   `6/4.awardinfo`、`7/7 type=1` 相同，调用
   `MoveBattleActorStep(item, itemId, count, seq, 0)` 做客户端增量入包。
2. 增量行的契约（见 `docs/re/2026-07-24-task-reward-backpack-refresh.md`）为：

   ```text
   tagged-i16 seq | tagged-u32 itemId | tagged-u32 count |
   tagged-i16 stackRuntime | tagged-i16 enhanceLevel | tagged-u8 attrCount=0
   ```

3. 旧 `vm_net_mock_append_trade_terminal_object` 把 `count` 写成
   `tagged-i16`（`00 02 <be16>`）。`stream_read_i32_be_tagged` 会无条件跳过
   当前两字节标签再读 4 字节值：跳过 `00 02` 后，把 `count` 的低字节与后续
   `common-extra` 的标签字节拼成错误的大数量（例如 `count=1, stackRuntime=1`
   时读成 `0x00010002`）。
4. 客户端按该错误数量对装备反复插入空槽，背包被同名 `(+N)` 行填满；服务端
   `trade_commit` 只写入一件，故 MySQL 仍为单行。这是 UI 与权威状态第一次
   偏离的位置，不是持久化双写。

同包内的 `21/6` 报价刷新走 `0x01025AE6` 交易面板解析器，仍使用 `i16` 数量，
不经过 `BuildShopBuyList`，本次未改。

## 修复

- `21/8 iteminfo` 的 `count` 改为 `tagged-u32`，与 `7/7` / `6/4` 一致。
- 装备收货按实例展开为 `count=1` 的多行（含各自 `destinationSeq`），避免单行
  `count>1` 再让客户端按数量复制。
- 装备 / 壶类的 `stackRuntime` 固定为可见数量 `1`；普通堆叠物仍写实际数量。

## 验证

1. 复测原日志路径：双方各报价 1 件装备并双确认，接收方背包只多 1 格，强化
   `(+N)` 正确，重登后与 MySQL 一致。
2. `make -j2` 通过。
