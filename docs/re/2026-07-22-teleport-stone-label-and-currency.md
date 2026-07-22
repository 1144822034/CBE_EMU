# 传送石地点标签与货币/库存语义（2026-07-22）

Status: implemented; server build and transport smoke test passed

## 当前卡点

打开传送石后，唯一的可传送地点显示为英文 `Penglai Home`；角色持有酷宝时，
点击地图传送会进入“余额不足/购买”提示，容易被误认为传送流程没有读取酷宝。

## 运行时与服务端证据

- `vm_net_mock_build_teleport_stone_exitinfo_blob()` 的 `16/1.exitinfo` 只产生一项，
  其默认 `label` 被硬编码为英文 `Penglai Home`。
- 同一 builder 已明确按 `u8 count, u32 exitId, len16 label, u32 reserved` 构造
  `exitinfo`，所以标签应直接作为该 GBK 字符串替换，不需要改变数组结构或传送目标。
- 默认目标场景来自 `vm_net_mock_default_scene_name()`，为
  `c00蓬莱仙岛_01.sce`；已有场景名称证据将其显示名定为“蓬莱-铜雀台”。

## IDA 证据

| binary | function/address | finding |
| --- | --- | --- |
| `江湖OL.CBE` | `HandleItemUseDispatch (0x010357E0)` | 仅把 `16/4` 交给 `HandleItemUseConfirm`。 |
| `江湖OL.CBE` | `HandleItemUseConfirm (0x010190A8)` | 对 `result=0/2` 读取 `value`，`result=0` 进入普通确认回调。 |
| `江湖OL.CBE` | `ConsumeInventoryItem (0x01018F66)` | 成功分支发送 `16/2`、`16/3`，随后按物品管理器类别 `14`、物品 ID `800` 逐个扣除 `value`。 |

`0x0101922C` 的 GBK 字节是 `传送石不够，是否购买？`。它位于
`ConsumeInventoryItem` 的本地库存不足分支；该函数没有读取或扣除酷宝字段。

## 契约与根因

### Request / response

- 请求：地图选择后 `WT 1/16/4 {curid,objid}`。
- 响应：`WT 1/16/4 {result=0,value=1}`。
- 客户端效果：确认后本地消费一枚 `itemId=800` 传送石，再发出 `16/2 + 16/3`
  场景退出链。

`value=1` 是传送石数量，不是酷宝价钱。酷宝只在商城的 `14/4.coolmoney` 显示和
`14/3` 购买 `itemId=800` 时参与扣费。因此“有酷宝但提示不足”在没有传送石库存时
是客户端的正常购买引导，不能通过将 `16/4.value` 改为零或让服务端直接扣酷宝来绕过。
那会跳过客户端的 `itemId=800` 扣除和背包更新契约。

## 追加取证：提示框发生在服务端之前

最新截图的文字与 `0x0101922C` 完全一致。`江湖OL.CBE` 的确认链为：

1. `HandleItemUseConfirm (0x010190A8)` 解析服务端 `16/4 {result=0,value=1}`，
   确认后进入 `ConfirmItemUseAction (0x010357B0)`。
2. 它调用 `ConsumeInventoryItem (0x01018F66)`；该函数先从**客户端本地**背包管理器
   查类别 `14`、物品 ID `800`。
3. 未找到时，不会向服务端发送扣费或传送请求，而是直接显示
   “您的传送石不足！请进入商城充值传送石享受服务。”；确认按钮才进入商城。
4. 找到时，客户端才发出 `16/2 + 16/3`，并在本地扣一枚 `800`；服务端随后进入既有
   场景切换链。

因此该弹窗无法、也不应以服务端的酷宝余额直接消除。酷宝和传送石是两个不同状态：
前者是商城余额，后者是可消费的背包物品。

本机关系型数据读取显示，两种状态可以独立存在；不能由“`wcoin > 0`”推导
“`itemId=800 > 0`”。为排除背包下发遗漏，使用一个已持有 `800` 的角色执行既有
`backpack-login-reseed-regression.php`，结果通过，首次 `30/21` 背包种子包长度为 601，
并包含 `itemId=800`。这证明持有传送石时服务端能将其正确下发至客户端本地背包。

## 本轮实现计划

1. 将默认 `16/1.exitinfo` 标签改为 GBK 的“蓬莱-铜雀台”；保留已有环境变量覆盖。
2. 在低频的传送石列表和商城打开日志中记录 active role、`itemId=800` 库存和
   `wcoin`，用于确认玩家进入商城后服务端返回的实际余额。
3. 保持 `16/4 {result=0,value=1}` 和 `16/2 + 16/3` 消费链不变。

## 取消后重试的列表加载（2026-07-22）

用户复现：背包已有传送石；第一次在本地确认框中选择“取消”后，再次触碰场景传送石，
界面停留在“获取数据”进度条。

### 客户端证据

按 `binary_name=mmGameMstarWqvga.cbm` 选择 IDA 实例后，
`sub_11CE(0x000011CE)` 对 `1/16/1` 读取 `exitinfo`，并在成功解析后清除
该网络 UI 的 loading 标志。`JianghuOL.CBE` 的
`ConfirmItemUseAction(0x010357B0)` 在取消（`0x2000`）分支仅关闭本地对话框，
不会发送取消 WT 包。因此第二次打开必须仍由服务端返回有效的 `16/1.exitinfo`；
取消本身不应改变物品库存、确认目标或场景。

### 服务端根因与修复

旧的 `vm_net_mock_is_teleport_stone_list_request()` 刻意只接受一个纯
`1/16/1` 对象。WT 运输层允许把这个只读列表查询与独立的场景刷新对象合并；用与用户
“取消后再次打开”状态等价的合并重试包复现时，旧服务没有进入列表 handler，记录为：

```text
[error][network] unhandled wt=16/1 len=24 objects=1 first=1/16/1:0,1/2/10:10
```

于是客户端从未收到 `16/1`，loading 无法收尾。这不是清除旧确认目标能够解决的问题。

将只读的 `1/16/1` 加入已有的“独立复合请求”显式白名单。该分发器会逐对象调用既有
handler，并合并已有响应对象；只允许 `16/1`，不拆分有顺序语义的 `16/2..16/4` 确认/
转移事务。

回归脚本 `tmp/teleport-stone-cancel-retry-regression.php` 模拟：登录角色（库存包含
`itemId=800`）→ 纯 `16/1` → 本地取消（无线包）→ `16/1 + 2/10` 合并重试。它断言两次
响应均包含 `1/16/1`。

## 场景传送石确认后的场景进入（2026-07-22）

### 运行时请求与失败点

持有 `itemId=800` 的角色在场景传送石上选择目的地后，服务端依次观察到：

```text
16/1                         -> 16/1.exitinfo
16/2 {exitID=1,type=3}       -> 16/2 result=2 的本地确认框
16/2 + 16/3 {exitID=1,type=3}
```

最后一个包长度为 64，并且没有 `7/1` 物品对象；这与
`JianghuOL.CBE:ConsumeInventoryItem(0x01018F66)` 的证据一致：客户端先发送
`16/2`、`16/3`，再在本地更新 `itemId=800`。旧服务在最后一个包错误记录：

```text
mock_teleport_stone_confirmed_exit_unresolved exit=1 provisional_scene=c00... provisional_row=1
unhandled wt=16/2 len=64 first=1/16/2:25,1/16/3:25
```

因此客户端收不到确认回调的 WT 应答，进度条自然不会结束。

### 正确的 `exitID` 解释

`16/2.exitID` 不是始终等于 `sMap.dsh` 行号：

- 先有 `16/4 {curid,objid}` 的世界地图流程中，它可以代表所选的子场景行；当其与
  已保存的父级目标不同，服务端据此精确查询 `sMap.dsh`。
- 场景传送石列表流程会在确认回调中重复列表项自身的父级目的地 ID。运行时的 `1` 与
  已保存目标的 `exitId=1` 相同，不能把它当作 `sMap` 行 1。

`vm_net_mock_refine_teleport_stone_confirmed_target()` 先比较确认 ID 与已保存目标：相同
（或为零）则保留该目标；不同时才查询 `sMap.dsh`。此规则在单独 `16/2` 和最终
`16/2 + 16/3` 两条路径共用。最终组合包仍仅回空 WT 物品确认，随后在下一次
scene-sync poll 通过既有 deferred `30/1` 进入场景，避免在确认回调内切图的历史崩溃。

## 验证

- `make -j2` 已通过；已以新二进制启动本地 mock service，并通过
  `CBMS`/`CBMR` transport ping。
- 传送石列表的默认标签已改为“蓬莱-铜雀台”。
- 无传送石时，客户端仍显示“传送石不够，是否购买？”，进入商城。
- 商城日志中的 `wcoin` 与角色当前余额一致；购买 `800` 成功后背包库存增加。
- 再次传送时客户端本地扣除一枚 `800`，并正常发送 `16/2 + 16/3`。

尚待客户端实际复测的是：点击本提示框的“确定”后进入商城，购买 `800`，返回背包后
再重复传送。该步骤是验证商城购买而非传送石确认包的契约。

客户端界面的最终确认仍需使用持有酷宝的测试角色执行一次“传送石 → 商城购买 →
返回传送石”的完整流程。为便于判断余额或库存不一致，服务端会依次记录：

- `mock_teleport_stone_list ... item800=<库存> wcoin=<酷宝>`；
- `mock_shop_open14 ... role=<角色> wcoin=<酷宝>`；
- 既有的 `mock_shop_buy14 ... wcoin=<购买前>/<购买后> insufficient=<0|1>`。
