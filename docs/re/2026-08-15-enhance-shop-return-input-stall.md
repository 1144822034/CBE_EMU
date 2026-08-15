# 强化页玄晶不足进入商城后背包输入停滞（2026-08-15）

## 触发步骤

1. 从背包中打开一件装备的强化页；
2. 在玄晶不足时选择前往商城；
3. 商城正常打开，按返回回到原背包；
4. 背包画面仍在，但所有点击均不再响应。

## 已确认的客户端链路

- `JianghuOL.CBE:HandleItemUseAndEquip(0x01028C7C)` 处理 `1/29/1` 与
  `1/29/2` 时都会清除 `itemCtrl+1468` 的网络等待位。因此已接收成功
  `29/1` 或 `29/2` 后，强化预览本身不应继续占用全局网络等待。
- `mmShopMstarWqvga.cbm:sub_1038` 的商城初始化只发送 `14/14`、`14/4`
  以及第一页 `14/5`、`14/6`；`sub_162C -> sub_11F0` 另发
  `1/1/14(actorId)`。
- `mmShopMstarWqvga.cbm:sub_9DE` 在收到最后一个 `1/1/14 actorinfo`
  对象后清除商城请求的等待位；该对象必须位于响应末尾。

## 本次运行时证据

`bin/server_out.txt` 中本轮强化后商城初始化为：

```text
29/1 (等级 3 预览) -> scene-interaction combo(14/14,14/4,14/5,14/6)
-> 1/1/14(actorId) response (actorinfo last) -> 7/42
```

没有场景重入响应；商城响应中的 `actorinfo` 也带有 parser 会读取的
`revivetype/ruffianflag/type` 字段。因此先前“商城返回导致场景栈替换”的根因不适用。

## 根因

服务端把 `g_netMockShop17ListPending` 当作“下一条任意背包列表请求都属于商城”的
全局开关。普通商城初始化会设置该标记，而商城关闭只是客户端本地 screen-pop，不会有
服务端可见的关闭包来撤销它。

这与已确认的请求契约冲突：

- NPC 商店兼容路径的列表请求是 **带非空 `17/1` 载荷**的
  `17/1 + 7/42` 组合；`vm_net_mock_build_shop_items_books_combo_response()`
  已严格验证这个条件。
- 普通背包打开/恢复使用空 `17/1`、空 `7/42` 或其空载荷组合，由
  `mmGame:sub_418C` 的背包列表 parser 消费；它不能被改写成商城项目列表。
- `mmShop:sub_1038` 的正常初始化只使用 `14/14`、`14/4`、`14/5`、`14/6`，
  另有 `1/1/14(actorId)` 状态查询；没有“将后续任意空背包请求改为商城列表”的客户端
  调用或 parser 证据。

旧分发器在严格的 NPC 组合 handler 之后，又把空 `17/1` 与空 `7/42` 单独路由给
`builtin-shop-items17` / `builtin-shop-items-books`。因此在“强化页 → 商城 → 返回背包”
中，遗留商城标记会污染恢复的背包列表，使可见背包与实际 list manager 的数据/选择状态
不再一致，后续点击被该失配的控件状态拒绝。

## 修正

1. 删除仅凭遗留 `g_netMockShop17ListPending` 把空 `17/1` 或空 `7/42` 改写为商城列表的
   两个分发分支及其专用 builder；
2. 保留带非空 `17/1` 载荷的 `17/1 + 7/42` NPC 商店组合 handler；
3. 让背包恢复的所有空列表请求重新走已有的角色背包 builders，不伪造关闭包、场景重入或
   客户端状态写入。

## 验证目标

1. 强化页玄晶不足进入商城、返回后，背包仍保留并能正常切换分类、选中物品和返回；
2. 同一会话的普通商城返回不再出现 `builtin-shop-items17` 或
   `builtin-shop-items-books`；空请求应由 `builtin-backpack-*` 处理；
3. NPC 商店的带载荷 `17/1 + 7/42` 仍由 `builtin-shop-items-books-combo` 响应。
