# Pixi chenghao incoming damage reduction

Date: 2026-08-17

## Rule

- Each **unlocked** Pixi 特效称号 id in `unlocked_chenghao_ids` CSV → **+1%**
  incoming damage reduction from monsters (normal attack + skill).
- Cap **45%** (matches current chenghao whitelist size).
- **PVP** (map spar / duel seats): reduction percent is **halved** (integer).
- Applied **after** armor soft-curve / resist %, before HP clamp.
- Not tied to currently equipped `eqChenghao` (stack by unlock count).

## Code

- `vm_net_mock_battle_apply_chenghao_incoming_dr` in `mock_server_role.c`
- PVE: `vm_net_mock_battle_enemy_damage_to_role_ex` /
  `vm_net_mock_battle_enemy_skill_remitigate_to_role`
- PVP: `vm_mock_service_duel_apply_operate` damage path

## Log

`mock_battle_chenghao_dr side=pve|pve-skill|pvp …`
