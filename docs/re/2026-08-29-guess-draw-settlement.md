# 猜猜看开奖结算误判（2026-08-29）

## 触发

开奖结果显示「大单」（或其它非豹子组合），押注「大」「单」等对应选项仍显示「未中」。

## 预期 / 实际

- 预期：非豹子且点数和 11–17、奇数 → 「大」「单」均 `win=1`，并按 2 倍扣 8% 手续费写 `payout_copper`。
- 实际：界面骰子提示正确（客户端 `guessDiceOutcomeHints`），但历史行几乎全是未中。

## 根因

`vm_mock_guess_draw_round` 用一条超长 `snprintf` CASE 结算押注，存在三处独立缺陷：

1. **缓冲区截断**：格式串约 850+ 字节，写入 `query[512]`，SQL 恒被截断；`tick` 又忽略 `draw_round` 返回值 → 轮次已标 DRAWN，押注仍 `win=0`。
2. **豹子条件写错**：写成 `d1=d1 AND d2=d2 AND d3=d3`（恒真），不是 `d1=d2 AND d2=d3`。
3. **实参少于占位符**（69 vs 67）→ CASE 错位，`b.choice` 常被比成第二颗骰子点数。

权威判定本就在 `vm_mock_guess_choice_wins`，但开奖路径未使用。

## 修改

- 新增 `vm_mock_guess_settle_bets`：用 `vm_mock_guess_choice_wins` 生成 `isBaozi/isBig/...`，再写短 UPDATE（带 overflow 检查）。
- 开奖顺序：写骰子 → 结算押注 → 再标 `DRAWN`；结算失败则清骰子并保持 OPEN，下轮 tick 可重试。
- 开奖 info 日志带上各选项中奖标志。

## 验证

- `make -j2` 通过。
- 下一轮开奖后：大单应对「大」「单」为中；豹子仅「豹子」中。
- 已错误结算的历史轮次不会自动回写；需要的话另做修复 SQL。

## 风险

`DRAWN` 更新失败时押注可能已按本次骰子结算，下一 tick 会重摇并覆盖 `win/payout`；概率低，需看 error 日志。
