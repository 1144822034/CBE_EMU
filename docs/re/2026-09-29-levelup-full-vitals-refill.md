# 2026-09-29 角色升级回满血蓝

## 现象

战斗结算升级后，服务端 `role_add_exp` 已把耐久 HP/MP 填满，但回地图左上角仍是战末残血；随后 `publish_role_vitals` 还可能用战斗全局残血盖掉耐久满值。

## 根因

1. `4/7.hp/mp` 只是壶/settle recover 增量，不含升级回满（见 `2026-06-25-battle-server-flow.md`）。
2. 结算后 `g_mockBattleRoleHpCurrent` 仍是战末残血；`publish_role_vitals` 用它覆盖 `role->hp`。
3. Pixi 只应用 recover 增量，未在检测到升级时把己方座位回满。

## 修改

- `vm_net_mock_role_apply_battle_settlement`：存活升级后把战斗全局 HP/MP（含新上限）对齐到 `role`。
- Pixi：`settlement.leveledUp`（相对本地等级）；`applySettlementRecover` 存活座位回满；`GameSession` 同步 `session` 血蓝。

## 验证

1. 残血打赢且经验升级 → 结算后座位满血蓝，回地图左上角满条。
2. 组队战死旁观升级（HP=0）→ 保持 0 血，不因升级回满。
3. 无升级的残血胜利 → 仍为战末/壶回复后值，不强制满血。
