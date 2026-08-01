# 三十倍经验卡 845：通用 7/1 卡住

## 触发与首次偏离

使用自定义物品 **845（三十倍经验卡）** 后客户端等待不结束。日志：

```text
[error][network] unhandled wt=7/1 len=42 objects=1 first=1/7/1:33
source=ignored-unhandled-server-only
```

首次偏离在服务端分派：请求是通用 `1/7/1`，但 845 被列入
`vm_net_mock_item_requires_special_use_protocol`，`build_item_use_response`
直接 `return 0`，客户端 `HandleItemOperationResponse` 等待永不解除。

## 客户端契约

| 物品 | 客户端请求 | 证据 |
| --- | --- | --- |
| 809/810/811 | `1/7/30 {itemseq,num=1}` | `JianghuOL.CBE:0x010236CA`（仅硬编码这三张） |
| 845（自定义） | `1/7/1`（通用使用） | 运行时 `wt=7/1 len=42`；CBE 无 845→7/30 分支 |

官方经验卡成功响应为 `1/7/30`，由 `0x01025AE6` 自行删行并置 `expcard`。
845 没有该分支，等待由 `0x01033544`（`7/1`）结束，删行依赖 `7/7`+`7/11`。

## 根因

845 从 811 克隆时正确接入了倍率/时效/专用协议分类，但 **未覆盖客户端
实际发出的 `7/1`**。专用协议分类对本意是挡住假成功的通用消耗；对 845
却变成空响应。

## 修复

在 `build_item_use_response` 中：若物品需要专用协议 **且**
`exp_card_multiplier_for_item != 0`，走
`vm_net_mock_build_exp_card_use_via_generic_7_1`：

1. 与 `7/30` 相同的 `EFFECT_EXP_CARD` 事务扣除与叠加；
2. 回 `7/1`+`7/7`+`7/11` 结束通用等待并同步背包；
3. 再附 `7/32 {result=1,expcard=1}`（官方 `7/30` 在
   `JianghuOL.CBE:0x0102643C` 本地写入的同一标志）以及 `7/31 {expinfo}`
   刷新左上角图标。

仅发 `7/31` 不够：自定义卡不经 `7/30`，客户端不会置 `expcard`，图标不显示。

809/810/811 仍由既有 `7/30` 路径处理；本分支是自定义卡的兼容契约。

## 验证

- [ ] 使用 845：日志出现 `mock_exp_card_generic_7_1`，不再
  `ignored-unhandled-server-only`；等待条消失，背包少 1 张。
- [ ] 左上角出现三十倍经验卡图标；打怪经验 ×30。
- [ ] 再使用可叠加时长；809/810/811 仍走 `7/30` 不受影响。
