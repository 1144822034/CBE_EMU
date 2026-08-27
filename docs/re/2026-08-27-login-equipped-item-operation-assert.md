# 登录已装备物品误走 7/7 type=2 的 InGame 断言

## 触发与首个偏离

player-3 在返回场景后的登录／组队初始化阶段记录到：

```text
item-timer category15_insert item=1001 seq=1 amount=1 max=1
manager=01056c10 logical=0/20 physical=74 occupied=0 empty=74
vMAssert(MMORPG_Screen_InGame.c:913, lr=051a6291)
```

`1001` 是已佩戴的木制宽剑。74 个物理槽均为空，故这不是背包满或物理槽泄漏；插入本身就是首个错误状态。

服务端的首次 `5/10 + 7/7(type=1)` 响应此前把 durable equipped rows 编码成
`WT 1/7/7 { type=2, iteminfo }`，并追加空的 `type=3`。固件的 `7/7 type=2` 分支会把行送入
`TimerControl_ProcessItem(0x01032EB8)`，即当前物品操作／插入路径，而非登录装备列表初始化。
这直接解释了 `category15_insert item=1001` 随后的 `MMORPG_Screen_InGame.c:913` 断言。

## 修复

- `vm_net_mock_append_backpack_role_grid_main_objects` 不再为登录响应生成 `7/7 type=2/type=3`。
  已装备数据仍保留在角色持久化状态；登录 `actorinfo` 已按服务端权威的完整派生属性提供 HP/MP 等数值。
- 移除了客户端宿主针对该登录结构的 WT 识别、拆包、双 event-7 原子投递与重试。该登录响应
  现在仅复制原始字节，并使用 CBE 登记的 event type、callback 和 context 投递一次。

装备图标／实例的真正首次登录协议仍为 `unresolved`：在有原始服务包和固件 parser 证据前，不得把
装备行伪装为任何 `7/7` 物品操作。

## 回归边界

- `first-login-equipment-attribute-bootstrap-regression`：首个 bootstrap 有且仅有必要的 `30/21`
  背包快照，明确拒绝 `7/7 type=2/type=3`，并验证 actorinfo 的完整派生 HP/MP 仍正确。
- `equipment-enhancement-bootstrap-split-regression` 与
  `equipment-enhancement-bootstrap-delivery-regression`：验证普通 data event 保持单个、原样的
  event-7 / response range / callback / context，不再为登录内容合成第二个事件。
