# player-1 商城返回 `InGame.c:913` 断言

phase: shop-return-safe-backpack-bootstrap
status: implemented; deterministic regressions passed

## 触发与首个偏离

player-1 从商城返回时报告：

```text
vMAssert(...MMORPG/_Screen/_InGame.c:913, lr:518ec89, ... last:c001410)
assert_stack[00]=0105346c
assert_stack[04]=0518fa55
```

该断言与已有的 `MMORPG_Screen_InGame.c:913` 负例具有同一不变量：已装备行不能在场景已
拥有的物品管理器中再次走新增物品通道。`src/server/mock_server_interaction_login.c` 在商城
ActorInfo 查询后的同角色 bootstrap 中，把 CBE 的自然 `7/7(type=3)` 请求识别为阶段二，
并追加了：

```text
1/7/7 { type=2, iteminfo=<durable equipped rows> }
1/7/7 { type=3, iteminfo=00 }
```

`mmGameMstarWqvga.cbm:sub_D04(0x0D04)` 会将这类行交给
`JianghuOL.CBE:TimerControl_ProcessItem(0x01032EB8)`。该函数是新增／合并物品路径，
不是刷新既有装备实例的路径；因此重放角色已装备行是首次被违反的协议契约，而不是商城
页面、场景加载或宿主网络回调的问题。

## 修复

- 删除登录／商城返回 bootstrap 中构造 `1/7/7 type=2` 装备行及其 `type=3` 空收尾的代码。
- 保留商城返回后由 CBE 自己发起的 `7/7(type=2)`：它仍得到完整 `30/21` 背包网格和正常
  `7/11` 储备计数（如适用）。
- 随后的 `7/7(type=3)` 仅得到其普通 `1/7/32` 状态响应，随后安全完成服务端的一次性
  bootstrap 标记；不会写入客户机内存、改变回调、拆分事件或修改包外状态。

## 回归

`first-login-equipment-attribute-bootstrap-regression` 现在验证阶段二响应中不存在
`1/7/7 type=2|3`，同时保留 `1/7/32` 以及 bootstrap 完成标记。

`shop-return-routing-regression` 覆盖同角色商城返回的重新 arm、自然 type-2 背包网格和
type-3 状态完成，并明确拒绝两个响应中的 `1/7/7` 重放对象。

本次已执行 `make -j2`，并以 `CBE_SERVER_TEST_INCLUDE_IMPLEMENTATION` 编译、运行以上两条
进程内回归。两者均通过；测试不启动监听器、不连接 MySQL，也不接触 player-1 进程或数据。

## 未决项

人物信息“装备”页面创建首次装备实例的真实服务端协议仍未确认。该 UI 缺口不能用本次已
证伪的 `7/7(type=2|3)` 路径修补；需要保留原始请求／响应包并按该页面专属 callback 继续
取证。
