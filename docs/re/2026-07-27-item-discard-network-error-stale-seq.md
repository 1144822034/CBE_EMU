# 丢弃提示「网络异常」：幽灵 seq / result=2

日期：2026-07-27

## 运行时证据

```text
mock_item_discard item=0 seq=40 count=0 remaining=0 result=2 refresh=7/4-fail resp=23
```

前序多为仓库 `26/1`。客户端对 `7/4 result=2` 显示「网络异常」。

## 根因

服务端背包已无 `seq=40`（常见于仓库**存入**后：`26/1` 不能同事件带
`17/1`，客户端主背包仍保留已存入行）。丢弃按客户端幽灵 seq 查找失败 →
`result=2`。

首次偏离：存入后未权威刷新客户端背包列表。

## 修改

1. 仓库存入成功：武装 `backpackListResyncPending`，poll 投递 `17/1+7/42`。
2. `7/4` 找不到行：按权威态已是「该行不存在」，回 `result=1` + `17/1+7/42`
   清幽灵，避免「网络异常」；日志
   `mock_item_discard_stale_resync ... bag_seqs=[...]`。

## 验证

1. 存入一件后立即打开背包丢另一件 / 点幽灵行：不应再 `result=2`。
2. 日志可见 `mock_backpack_list_resync` 或 `mock_item_discard_stale_resync`。
