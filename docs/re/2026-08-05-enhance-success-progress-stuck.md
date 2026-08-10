# 强化成功后进度条卡住

日期：2026-08-05

## 症状

装备强化确认成功（有 `29/3 result=1` / 等级提升 / 世界公告）后，客户端
仍停在等待进度条，需断线或重登才恢复。

## 运行时证据（`bin/dlog.txt`）

```text
mock_equipment_enhance bag-list-arm … inline_17=1
  evidence=17/1+7/42-after-enhance-attrs
mock_equipment_enhance phase=3 … success=1 inv_sync=1
  resp=29/3+7/7-type2+7/11-crystal+equip+10/26+17/1+29/1
mock_backpack_list_resync … phase=1-done
  evidence=…-list-only-no-26/0
```

同包对象构成（1 玄晶）：`29/3` + 晶 `7/7+7/11` + 装 `7/7+7/11` +
`10/26` + `17/1` + `7/42` + `29/1` ≈ **9**。多玄晶时易 **>10**。

## 根因

1. **被违反的契约**：`event_packet_parse_WT` 对象数上限 **10**
   （`docs/re/2026-07-20-new-role-login-object-cap.md`）。超限 → 解包错误 /
   进度条不收。
2. **`17/1` 同包副作用**：权威背包列表会再开
   `DF_DataPackage_DoLoading`；仓库存入路径用后续 **`26/0`** 收条。
3. **首次错误状态**：强化成功路径把 `17/1` 塞进 `29/3` WT，并把 poll
   标成 `listOnly`（**故意不发 `26/0`**）。客户端在强化 wait 已由
   `29/3+7/11` 处理后，又被 `17/1` 拉起 loading，且永远等不到 busy-ack。

崩溃/卡死是最终症状；偏离点是 **成功回包对象契约**，不是强化公式。

## 修改

1. `29/3` 成功同包只保留：`29/3` + 玄晶/装备 `7/7+7/11` + `10/26` +
   预算内 `29/1`；**不再 inline `17/1`**。
2. 同步路径遵守 `VM_NET_MOCK_WT_EVENT7_OBJECT_CAP=10`；溢出玄晶改由后续
   `17/1` 权威列表补齐。
3. `bag-list-arm`：`listOnly=0`，poll 先 `17/1(+7/42)` 再 lone **`26/0`**。
4. 转移确认同样去掉 inline `17/1`，改 poll+`26/0`。

## 验证

1. 单晶 / 多晶强化成功：进度条立即消失；日志
   `resp=…+10/26+29/1`（无 `+17/1`），随后
   `bag-list-arm … poll-17/1+26/0` → `phase=1->2` → `busy_ack`。
2. 打开背包：强化等级与强化附加正确；玄晶/铜币正确。
3. `make -j2`。
