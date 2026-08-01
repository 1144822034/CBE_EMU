# 仓库取回未强化装备显示为强化+1

日期：2026-07-27

## 现象

未强化装备存入仓库后再取回，客户端显示为强化 1（`(+1)`）。
服务端日志已是 `mock_backpack_add ... enhance=0`，且随后有
`17/1+7/42` resync，仍显示 `(+1)`。

## 根因

取回经 `npcPurchaseBackpackPending` 投递 `7/7 type=1` 增量入包。
common-extra 布局为：

```text
tagged-i16 first | tagged-i16 second | tagged-u8 attrCount=0
```

客户端把**第一**份 `i16` 低字节映到物品 `+0xe`（背包名 `(+N)`），第二份映到
`+0xf`（上限，与 `29/1`/`0x010287C0` 一致）。

旧取回路径把装备的 `first` 写成 `1`（误当作“可见堆叠=1”），未强化时线上为
`(1, 0)` → `(+1)`。

首次偏离：`7/7` / `17/1` / 交易收货 common-extra 的**第一** `i16`。

## 修改

1. 装备：`first = enhanceLevel`，`second = maxlevel`（见
   `2026-07-29-login-backpack-enhance-zero.md`）；禁止再写 stack=1。
2. 非装备：`first = stackRuntime`，`second = 0`。
3. 统一入口 `vm_net_mock_seq_put_item_common_extra` /
   `vm_net_mock_item_common_extra_stack_byte`。
4. 取回后仍武装 `backpackListResyncPending`（`17/1+7/42`）。

## 验证

1. `make -j2`，重启服务。
2. 未强化装备：存入 → 取回 → 背包无 `(+1)`；日志
   `mock_backpack_add ... enhance=0`。
3. 强化 ≥1 的装备取回后等级不变；重登背包亦显示 `(+N)`。
4. 怪掉 / 交易获得的未强化装备同样无 `(+1)`。
