# Pixi nearby companion: `plus16info`

phase: pixi-plus16-aura-prep  
date: 2026-08-09

## Goal

Expose “how many worn slots are enhance ≥ 16” for nearby peers so Pixi can
drive a map-side gem sparkle without polling `29/4` per actor.

## Why not extend `otherinfo`

`ParseSceneOtherNodeData(0x01012958)` / `HandleFactionOtherInfoResponse(0x01031162)`
consume a fixed per-row stream (`actorId … gridPos`). Player and NPC rows share
that layout. Appending bytes inside the row risks misalignment.

## Contract (Pixi-only)

On the same WT object that already carries nearby roles:

| Object | Existing fields | New optional field |
|--------|-----------------|--------------------|
| `1/2/10` | `othernum`, `otherinfo` | `plus16info` (raw) |
| `1/2/7`  | `result`, `pgIdx`, `allpgs`, `othernum`, `otherinfo` | `plus16info` (raw) |

`plus16info` blob (parallel to `othernum`, keyed by `actorId`):

```text
repeat N times:
  u32 actorId
  u8  plus16Count   // 0..8, slots with enhance >= 16
```

CBE looks up `othernum` / `otherinfo` by name and ignores unknown fields, so
this companion must not change CBE nearby spawn / 周围玩家 list behavior.

## Server source

`vm_mock_service_client_session.onlineEquippedEnhanceLevels[8]`, refreshed by
`vm_mock_service_capture_session_presence` from `role->equippedEnhanceLevels`.
Count uses `VM_NET_MOCK_EQUIP_ENHANCE_MAX_LEVEL` (16).

Builders:

- `vm_net_mock_build_plus16info_from_seeds` / `vm_net_mock_build_scene_plus16info_blob`
- Attached from `vm_net_mock_append_actor_other_scene_roles10_object` (map peers)
- Attached from `vm_net_mock_build_scene_ctrl_page_response` when the page lists peers

Empty `2/10` (no roles) omits the field.

## Pixi client

- `PeerActor.plus16Count?: number`
- `parsePlus16Info` / `applyPlus16Info`
- `parseNearbyPeers` (2/10) and `parseNearbyPage` (2/7) merge by `actorId`
- `demoBuildPollPeers` emits the companion for smoke

## Map VFX (Pixi)

`Plus16Sparkles` (`PixiJS_web/src/render/Plus16Sparkles.ts`):

- **One concentric ring per +16 piece** (max 8), each a fixed rainbow color.
- Centered on the actor (lifted ~8px from feet); portal-like breath
  (scale 0.82→1.1 + alpha pulse), rings slightly phase-offset.
- Local via `onWorn`, peers via `plus16Count`.

## Still open

- Count on peers refreshes when the next `2/10` / `2/7` baseline is rebuilt;
  mid-scene enhance without a new otherinfo pass leaves observers on the last count.
