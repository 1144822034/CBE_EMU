# 鬼道单体治疗只能加自己（2026-07-30）

## 症状

队伍战斗中，鬼道 `清风拂面`（skill.dsh `目标指向=1`）点选队友施放，只有自己回血，队友不涨。

## 根因

1. `vm_net_mock_battle_apply_player_friendly_group_heal_targets` 对 `td=1` 写死
   `member != actor → continue`，只结算施法者。
2. `4/2.index`（客户端选中单位）在进治疗分支前被
   `target_wire_slot_from_request` / `playerSlot→enemySlot` 改写成敌方 wire，
   友方选择被丢掉。

`目标指向=2` 的群体治疗（三花聚顶）不走该跳过，故表现正常。

## 修改

1. `td=1` 按 `4/2.index` 解析目标队员（接受 member wire / display / 旧
   `display_to_wire` 编码），对该队员加血并写入对应 actioninfo child。
2. 友方治疗/增益不再把 index 重映射到敌方；无效目标时回退施法者。
3. 日志：`mock_battle_single_ally_heal ... evidence=skill.dsh:目标指向1`。

单人战斗仍只治疗自己（无队友座位）。切磋 duel 路径仍是自疗（1v1 无队友）。

## 验证

1. `make -j2 server`，重启服务端。
2. 双人组队遇怪：鬼道对残血队友放清风拂面 → 队友 HP 上升；日志含
   `mock_battle_single_ally_heal` 且 `target_member` 为队友座。
3. 对自己施放仍可回血；三花聚顶群体治疗回归不受影响。
