# 2026-08-24 PVP 切磋/比武：敏捷高的一方动画后手

phase: pvp-duel-agility-turn-order
status: fixed

## 症状

玩家反馈：PVP（切磋 / 比武 / 赏金）双方提交操作后，**敏捷更高的一方动画上反而后出手**。

## 契约（预期）

1. 双方 `4/2` 均提交（或超时补普攻）后，`duel_round_resolve` 按**当前敏捷**高者先结算。
2. 每一击单独一条 `4/6`，WT 内按 serial 顺序合并投递；客户端按序播放全部 actioninfo。
3. 物理未命中须带 `child_flag=3`（闪躲），客户端飘「闪躲」——不能只靠 `valueA=0` 且不发动作。

## 调查结论

### A. 先手序本身（服务端 seat 顺序）——**无反转**

`duel_begin` 冻结 `firstTurnIndex`：

```c
firstTurnIndex = (responderAgi > inviterAgi) ? 1 : 0;
order[0] = firstTurnIndex;  // 先播
```

`tmp/spar_duel_wide.txt` 回归：`agility=117/104 first_turn=0` → `duel_hit actor=0` 先于 `actor=1`。seat 映射与 wire 镜像（`build_duel_action_packet`）一致。

### B. 第一次偏离（根因）——**闪躲击被整段丢弃**

`duel_apply_operate` 未命中时返回 `damage=0, childFlag=3`（与 PvE 一致）。

但 `duel_resolve_committed_round` 有：

```c
if (!hit.supportNoDamage && hit.damage == 0)
    continue;  // 不生成 4/6
```

敏捷高者先结算；若其普攻/物技被闪躲，**整段攻击动画不会下发**。随后低敏捷方命中成为合并包里**唯一可见**的一击 → 观感为「敏低先出手 / 敏高后出手」。

高闪躲 build 的 PVP 对局里此现象更频繁，易被误报为「敏捷判定反了」。

### C. 次要缺口——**先手序仅在 duel_begin 算一次**

`神臂担山` 等战斗 buff 会改 seat 敏捷，但旧逻辑不在每回合 resolve 前重算。buff 后面板敏捷已反超，出手序仍用开战快照 → 偶发「我敏捷更高仍后手」（非系统性反转）。

## 修改

| 层 | 改动 |
|----|------|
| `mock_server_battle.c` | `childFlag==DODGE` 的 0 伤害进攻仍生成 4/6 |
| `mock_server_battle.c` | `vm_mock_service_duel_compute_first_turn_index`：每回合 resolve 前按 seat modifier 重算 `firstTurnIndex` |

## 验证

1. `make -j2`
2. 双端切磋：A 敏 > B，A 普攻被 B 闪躲 → **两段**动画（A 出手 + B 飘闪躲，再 B 反击）
3. 带敏捷 buff 技能后下一回合：buff 方应先手（日志 `duel_round_resolve … first=` 与当前敏捷一致）

## 仍未知

- 原版 CBE 是否每回合重算敏捷先手（现按 RPG 常规实现；开战不等快照）
- 若双方同回合均闪躲，仍可能出现 `duel_round_resolve_empty`（需另案：空回合重开提交窗）
