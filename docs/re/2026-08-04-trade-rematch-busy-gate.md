# 2026-08-04：交易连开占槽 / 同意变拒绝（对照切磋）

## 背景

切磋在 `release_not_before` 窗口内会出现「邀请可排队、同意却 `accepted=0` /
`spar-result=2`」。交易没有长 hold，但上一笔结束后若 `21/7|21/8` 仍挂在
`terminalPendingMask` 上，槽保持 `used`：

- `trade_begin` 因任一方仍占槽直接失败；
- 同意路径把 `result` 改成 `2` 并投递 `TRADE_RESULT`；
- 邀请路径原先不检查忙状态，仍可排队弹窗。

## 修改

- 已结束（`!active` 且无 offer pending）的槽可在邀请/开战时强制释放
  （可丢掉尚未 poll 到的 terminal，换取立即可再交易）。
- 仍在 `active` 或有 offer pending 时拒绝新邀请（不排队，`queued=0`，
  `reason=active-trade`）。发送方仍是本地「已发送」空 WT 确认（协议原样）。
- `trade_begin` 失败时打 `mock_trade_reply_begin_failed` 便于对照。

## 正常路径

邀 → 同意 → 放物品 → 确认：不变。仅连开/忙状态边界行为与切磋对齐。

## 复测

1. 完整交易一笔后稍后再交易：应 `trade_session_begin` + 双方进窗。
2. 确认后立刻再邀再同意：允许（可能见到 `trade_session_release ...
   rematch-begin-*` / `trade-invite-clear-finished`）。
3. 交易进行中再邀：`mock_trade_request_busy`，对方无新弹窗。
