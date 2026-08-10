# 结算未拆完再开战：prepend type=0 导致第二场不能操作

日期：2026-08-05

## 现象

梦境退店后打怪正常；打完一场立刻踩到下一只 → 进入第二场，**玩家不能操作**。

## 日志（`48019507` / `lxh001`）

```text
mock_battle_settlement_exit_arm … delay_ms=8085
# 延时未到，无 poll-delayed 4/8
mock_battle_start_blocked_by_settle … action=prepend-exit-reenter … objects=6
mock_challenge_battle_start … objects=6
# 随后无玩家 4/2，仅见敌方回合 / idle-timeout
```

## 根因

1. 胜利后 `settlement_exit` 约 8s 才 poll 下发；其间仍可走路触怪。
2. 第二场 `4/1` 走 `prepend-exit-reenter`：同包先塞 `4/8+4/11 type=0+4/9` 再开战。
3. **首个错误状态**：`4/11 type=0` 叠进新战斗壳 → 操作栏失效（与
   `2026-07-30-settle-operate-menu-stall.md` 同族；挂机路径已对 settle 跳过 type0）。

## 修改

`release_settle_for_start` prepend 只发 **`4/8`**，不再带 type0/4/9。
正常 poll 离场仍走完整 `4/8+4/11+4/9`。

## 验证

```text
mock_battle_start_blocked_by_settle … evidence=4/8-only-before-1/4/5-no-type0
mock_challenge_battle_start …
# 第二场可点技能/普攻
```
