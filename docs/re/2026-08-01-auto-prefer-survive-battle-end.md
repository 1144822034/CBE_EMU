# 自动战斗 prefer：战斗结束保留，仅按 1 关闭

日期：2026-08-01

## 症状

非挂机点自动后，一场打完再遇怪需再按 1；期望与挂机类似，跨场保持到手动关。

## 根因

`mock_challenge_battle_start` 在组包时无条件：

```text
g_mockBattleAutoPrefer = 0;
g_mockBattleLastOperateValid = 0;
```

其后「若 prefer 则追加 4/11 type=1」成为死代码。结算拆场本身已 `prefer_kept`，
但下一场 challenge 又把 prefer 清掉。

## 契约

| 事件 | prefer |
|------|--------|
| `4/11 type=1`（按 1 开） | 置 1 |
| `4/11 type=0`（按 1 关） | 清 0 |
| 胜利结算 / `4/8` 拆场 | **保留** |
| 下一场 solo challenge | **保留** + 开战带 `4/11 type=1` + arm synth |
| 组队开战 | 仍 `suspend_solo_auto`（回合屏障） |
| 挂机 stop-after | 仍清 prefer（挂机按钮语义） |

## 修改

`mock_server_battle.c` challenge start：去掉跨场清 prefer；solo 保留时追加
`4/11 type=1`；无 prefer 时才清 stale last-operate。

## 验证

1. 遇怪 → 按 1 开自动 → 打完 → 再遇怪：日志 `mock_challenge_battle_auto_keep`
   + 开战含 `4/11`，无需再按 1 即出手。
2. 自动中按 1：`type=0`，本场与后续不再自动。
3. 组队遇怪仍 suspend，不走 solo synth。
