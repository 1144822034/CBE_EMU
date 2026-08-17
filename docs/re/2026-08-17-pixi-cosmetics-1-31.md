# Pixi cosmetics unlock / equip / nearby (WT 1/31)

Date: 2026-08-17

## Goal

Server-gated Pixi-only 特效称号 / 翅膀 / 坐骑: unlock lists + equipped ids, visible to nearby Pixi peers. Do **not** reuse `special_designation_mask`, classic `23/u8`, or put `chenghao:` into `titleBadge` for CBE.

## Storage

- Role columns (CSV + equipped ints): see `server/mysql/migrate_add_pixi_cosmetics.sql`
- Role fields on `vm_net_mock_role_state`; DB version 6
- Unlock source: admin grant (+ reserved gameplay hooks via `try_unlock_*`)
- Role DB format marker: `account_role_state.format_version` 5→6; load accepts 5 and upgrades on save (strict `==6` emptied title role lists).

## Wire

| Direction | Object | Meaning |
|-----------|--------|---------|
| C→S | `1/31/1` | Query unlock + equipped |
| S→C | `1/31/2` | State: CSV strings + `eqChenghao/eqWing/eqMount` |
| C→S | `1/31/3` | Equip: `kind` u8 (1/2/3), `id` u32 (`0` unequip) |
| S→C | `1/31/4` | Equip ack: `result` + state fields when ok |
| S→C | `1/31/5` | Peer notice: `actorId` + three equipped ids |

- Classic title select (`23/3`) clears equipped chenghao and broadcasts `31/5`
- Nearby baseline / moveinfo visibility enqueues peer `31/5`
- Admin grant sets `cosmeticsStatePushPending` → next scene-sync poll may append `31/2`

## Client

- `GameSession.syncCosmetics` / `equipCosmetic` / gated title list + wing/mount panels
- Self overlays via `onCosmeticsUpdate` → `SceneView` wing/mount/nameplate
- Peer overlays via `onCosmeticsPeer` → `ActorLayer.applyPeerCosmetics`
- Pixi local walk: wing ignores MAP terrain; mount walk speed ×2
- Incoming DR: **+1% per unlocked chenghao** (CSV count), cap **45%**; applied after
  armor/resist on monster NA/skill hits; **PVP (duel) percent is halved**

## Combat: chenghao incoming DR

- Stack source: **unlocked** Pixi 特效称号 count (`unlockedChenghaoIds` CSV), not currently equipped id.
- PvE (monster NA + skill → player): `-1%` per unlock, cap **45%**.
- PvP (duel spar/wager/arena): same percent **halved** (`pct/2`, integer).
- Applied after armor/resist mitigation in:
  - `vm_net_mock_battle_enemy_damage_to_role_ex`
  - `vm_net_mock_battle_enemy_skill_remitigate_to_role`
  - `vm_mock_service_duel_apply_operate` (target seat)

## Verify

1. Admin unlock wing/mount/chenghao for a role (or unlock-all of kind)
2. Enter world → panel lists only unlocked ids; equip via `31/3`
3. Second Pixi client on same map sees peer wing/mount/chenghao without classic `23` gif spoof
4. Select classic designation → peer chenghao clears (`31/5` eqChenghao=0)

## Unresolved

- CBE clients ignore `1/31` (unknown objects); no CBE wing/mount rendering
- Admin grant while online relies on dirty persist + poll `31/2`; re-login always safe
- Pixi local walk bonuses: wing → ignore MAP terrain; mount → speed ×2 (client-only)
- Chenghao DR: unlocked count ×1% (cap 45%); PvP half; see Combat section above
