# 黄金宝箱连续开启耗尽物品物理槽

Date: 2026-08-18

> 2026-08-19 correction: the chest/key slot-leak fix remains unchanged, but
> the reward is now delivered by the firmware's native `7/15` result instead
> of mmGame `7/7 type=1`; see the final response below.

## 触发与证据

角色进入场景已经稳定后，连续开启黄金宝箱。每次成功响应后，运行时只读探针都记录
宝箱 `524` 和黄金钥匙 `815` 再次进入 `TimerControl_ProcessItem(0x01032EB8)`：

```text
category15_insert item=524 seq=321 amount=58 physical=74 occupied=73 empty=1
category15_insert item=815 seq=258 amount=42 physical=74 occupied=74 empty=0
```

下一次需要物理记录时，客户端无法取得空槽，最终以空物品指针进入
`0x01031EEA`，在 `0x01031EF4` 读取偏移 `0x11A` 时崩溃。该 PC 是耗尽后的
最终症状，不是修复点。

当时服务端同一请求发送 280 字节、六对象响应：

```text
7/7 type=2 + 7/11  宝箱
7/7 type=2 + 7/11  钥匙
7/7 type=1         奖励
7/37 result=1      可见奖励提示
```

## 客户端契约

- `mmGameMstarWqvga.cbm:sub_11CE(0x11CE)` 将每个 `7/7` 交给
  `sub_D04(0x0D04)`。
- `sub_D04` 不会忽略 `type=2` 的 `iteminfo`。它先完整解析物品行，再在
  `0x1062-0x1072` 把该行交给客户端物品管理接口。
- `JianghuOL.CBE:TimerControl_ProcessItem(0x01032EB8)` 对类别 15 按物品 ID
  执行加法堆叠；找不到可合并记录时会占用新的 324 字节物理槽。因此把服务端
  “剩余数量”放入 `type=2 iteminfo`，实际语义是再次添加这一数量。
- `JianghuOL.CBE:HandleItemOperationResponse(0x01033544)` 的 `7/11` 分支读取
  `{seq,count}`。它在 `0x010336B8` 按序号找到现有行，数量大于零时直接写回，
  数量为零时在 `0x010336EC-0x010336F4` 调用物品管理器删除路径；处理结束还会
  清除挂起物品操作。该对象已经完整拥有宝箱和钥匙的同步生命周期。

## 根因

开箱响应错误地把 `7/7 type=2` 当成“按序号更新已有行”，并为宝箱和钥匙分别
携带剩余数量。客户端实际把这两行当作新增数量送入类别 15 堆叠器；随后的 `7/11`
只修正目标序号的可见数量，无法撤销已经创建的额外物理记录。每次开箱因此泄漏两个
物理槽，直至固定的 74 槽耗尽。

## 修复

开箱成功响应改为：

```text
7/4  result=1                 静默完成并清除操作等待状态
7/11 { info = chest_seq, chest_remaining }
7/11 { info = key_seq, key_remaining }
7/15 result=1,total=1  原生奖励增量与提示
```

删除的只是两个违反客户端契约的消费项 `7/7 type=2` 对象。服务端事务、宝箱和钥匙
持久化扣除保持不变；数量归零仍由客户端原生 `7/11` 删除分支释放物理记录。奖励
后来改由主固件 `HandleShopBuyItem(0x01025AE6)` 的原生 `7/15` 分支一次性插入并显示
“获得%d个%s”，不再使用 `7/7 type=1` 或系统聊天绕行。

## 回归边界

`scripts/chest-open-reward-notice-regression.c` 现在断言成功包只有四个对象，顺序为
`7/4, 7/11, 7/11, 7/15`，并明确拒绝消费项的 `7/7 type=2`。运行时复测还需确认：

- 连续开箱时不再出现 `category15_insert item=524/815`；
- 奖励物品按原生 `7/15` 正常加入且只加入一次；
- 宝箱或钥匙归零时对应物理记录被释放；
- 不再到达 `pc=0x01031EF4`。
