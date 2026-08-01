# 梦境自定义名牌 ScreenInit 崩（2026-07-31）

## 与上一轮的区别

上一轮是 `29*_01.map not-published`。本轮资源链路已正常：

```
mock_update_chunk_uptodate file=29梦境空间_01
mock_update_chunk_uptodate file=29梦境空间_03.map
… e_ghostfireB/G.actor + gif 全部 uptodate
```

## 首次偏离

选角拉齐资源后，mmGame `ScreenInit`（`init=0x0502eba5`）期间：

- `地址无法访问:4ad5542`
- `pc=0x4ad5542`（未映射）、`lr=0x010136b3`、`lastPc=0x010136b0`
- `cpsr` thumb=0（把垃圾当 ARM 入口跳）
- 栈附近有 `ui_h_war.actor`

服务端停在 `group-type1`，尚未 `scene_ready`——崩在进图解析/造点，不在开战包。

## 根因陈述

SCE kind=3 field `0x0F` 被写成目录自定义名「梦魇」（GBK `c3cef7ca`），线号仍是 `#200` + ghostfire。

对照：同一结构写「幽冥鬼火」时可进图并 `wire=200 real=203` 开战。改为「梦魇」后，在资源全部命中缓存的情况下 ScreenInit 跳飞。

**被违反的契约**：FB 地图名牌必须使用线号存量显示名；自定义身份只通过绑定表在开战 remap，不能进 SCE `0x0F`。

## 处理

1. 写回逻辑恢复：FB / wire≠real 时 field `0x0F` 强制线号存量名。  
2. 磁盘 `29梦境空间_01` 三处名牌已改回「幽冥鬼火」。  
3. 需重启 mock 后**后台再保存一次刷怪点**（或等价 publish），让 WT18/7 版本变化，客户端丢掉仍含「梦魇」的本地 SCE。

## 仍未知

`0x010136b0` 精确函数名（需 IDA）；为何自定义名牌会打坏后续函数指针。不影响上述契约结论。
