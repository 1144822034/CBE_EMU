# 强化到 +4 后背包强化附加不同步

## 症状

背包装备从强化 1 升到 4 后，详情里「强化附加」仍缺词条；`( +N )` 也可能仍旧。
退出商城或重新登录后词条出现。MySQL / 内存 `enhance_level` 已正确。

## 根因

`29/3` 成功路径已追加装备行 `7/7 type=2`（含 L≥4 common-extra 词条），但
`mmGame:0xD04` 对装备走 item-manager `+104` 且 `r2=-1`，**不会可靠地重写
已有背包行的完整 ParseEquipAttributes**（尤其 `attr_count` 从 0 扩到 4）。

出商城 / 重登会经 `17/1` 或 `30/21` 重建 item-manager，故能带出权威词条。
首次偏离：强化成功后客户端背包行仍停在强化前的 common-extra，而非存档错误。

与仓库存装备后需 `17/1+7/42` 刷新同一类契约
（`2026-07-27-warehouse-deposit-client-bag-not-refreshed.md`）。

## 修改

`29/3` 强化成功且已做 inventory sync 时：

1. 同包追加权威 `17/1`（+ best-effort `7/42`），iteminfo 带当前 enhance 与词条。
2. 武装 scene poll 的 list-only `backpackListResync`（只推 `17/1+7/42`，**不**发
   仓库用的 `26/0`），防止强化 UI 未消费同包 `17/1` 时仍缺刷新。

## 验证

1. 背包武器/衣服 3→4 成功后，不进商城、不重登：打开详情可见 4 档强化附加
   （第 1 亮、后 3 灰）。
2. 日志含 `bag-list-arm` / `+17/1`；poll 可见 `list-only-no-26/0`。
3. 玄晶数量与铜币仍正确；`make server -j2`。
