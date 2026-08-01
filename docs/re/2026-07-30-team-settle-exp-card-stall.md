# 组队结算 + 十倍/三十倍经验卡住（2026-07-30）

## 症状

玩家反馈：组队战斗胜利结算时，若使用了 **十倍经验卡（811）**，客户端会卡住。
三十倍（845）同路径，更容易触发。

## 根因

组队终局对死亡观察者仍发 `forceTeamVictory` 的胜利 `4/7`，并要求保留真实
`HP=0`（见 `2026-07-24-team-battle-terminal-peer-crash.md`）。

结算写库路径：

1. `vm_net_mock_role_apply_battle_settlement` 先写入战斗 HP（可为 0）
2. `vm_net_mock_role_add_exp` 在升级时把 `hp/mp` **拉满**
3. 经验卡 ×10 / ×30 使单场升级很常见

于是权威状态变成「满血」，而 Battle.cbm 仍按阵亡单位渲染/拆场，契约被破坏，
表现为结算后卡住。

存活队员升级拉满仍是预期；仅 `settleHp==0` 必须在升级后重新钉死为 0。

## 修改

1. `apply_battle_settlement`：升级后若结算 HP 为 0，强制保持死亡 HP/MP；
   `normalize` 后再校验一次。日志：`mock_battle_settle_keep_dead_hp`。
2. 开战清零 `g_vm_net_mock_battle_rewarded_serial`（与 rewarded_exp 一并），
   避免跨场串号。

## 验证

1. `make -j2 server`，重启。
2. 双人组队：一人先死，另一人击杀；死亡者开着 811 或 845。
3. 双方应能正常出结算并离场；日志可有 `mock_battle_settle_keep_dead_hp`。
4. 双人都存活 + 经验卡：结算/离场正常；经验按各自倍率发放。
