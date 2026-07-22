# NPC 服务商人购买后的背包更新（2026-07-22）

## 症状与首个分歧

从场景动态 NPC 的商品菜单购买成功后，角色数据库中的铜钱和背包行已
更新，但客户端背包没有出现新物品。

原实现把 `1/17/1` 全量背包对象附在 `1/26/1` NPC 对话响应之后。这个
对象适用于背包/商店列表的主动查询；它不是动态 NPC 服务购买完成时的
物品管理器更新包。

## 客户端证据

IDA 按二进制名选择实例：`江湖OL.CBE` 与
`mmGameMstarWqvga.cbm`。

- `江湖OL.CBE:net_business_dispatch_by_subcmd(0x01012E4C)` 只直接分发
  场景业务 kind；`17` 不在其 switch 中。
- `mmGameMstarWqvga.cbm:sub_11CE(0x11CE)` 在后续模块回调中只处理此
  路径所需的 `14/3`、`7/7` 和 `16/*`，同样不会消费随 `26/1` 到达的
  `17/1`。
- `mmGameMstarWqvga.cbm:sub_D04(0x0D04)` 解析 `1/7/7` 的
  `type` 与 `iteminfo`。当 `type=1` 时，它把每一行传给
  `TimerControl_ProcessItem`。
- `江湖OL.CBE:TimerControl_ProcessItem(0x01032EB8)` 按客户端背包的
  堆叠规则合并同类物品，或插入空槽。因此该行必须表示本次获得的
  **增量**，而不是服务端堆叠后的总数量。

## 修复后的契约

动态 NPC 商人购买成功的同一响应现在是：

```text
1/26/1 { dialog=购买成功后的菜单 }
1/7/7  { type=1, iteminfo=[一条本次购得物品的增量行] }
```

普通可堆叠物品的增量为 `1`。逍遥壶/神仙壶等储量型物品保持独立行，
其 wire count 使用新行的 HP/MP 储量，公共属性仍声明一个可见壶。

`17/1` 仍只用于背包或商店列表模块主动发起的全量查询，避免在错误的
客户端回调路径中伪造刷新。

## 相关替换装备保障

已确认客户端 `BuildGameEventPacket(0x010328D4)` 对替换装备使用
`1/7/9 { body:u16, bag:u16 }`；
`HandleItemOperationResponse(0x01033544)` 只读取响应中的 `result`。
现有专用 handler 保持 `1/7/9 {result}` 响应，并在持久化失败时回滚角色
内存状态、返回失败结果，避免客户端显示成功而数据库没有完成替换。
