# Scene monster battle-start crash (2026-07-22)

Status: root cause identified; server target-source correction implemented and
targeted service regression passed.  Client visual/runtime confirmation remains
to be performed on the original collision path.

## Reproduction and observed failure

1. Enter a scene containing a combat monster.
2. Move the player into the monster so the client sends the normal `WT 1/4/1`
   challenge request.
3. The service returns a 191-byte event-7 response.
4. During the first battle frame the client faults at `JianghuOL.CBE:0x01004EA8`
   while reading address `0x0000000c`.

The failing render object at `r4=0x01053B18` starts with `(x,y)=(40,140)` and
has a null pointer at offset `+0x0C`.  `(40,140)` is a left-side battle-unit
layout coordinate, so the final symptom is a battle monster whose visual
resource context was never populated.  This is not a scene-map tile fault.

## Packet and parser chain

```text
scene monster collision
  -> client WT 1/4/1 { id, index, posx, posy [, moveinfo] }
  -> vm_net_mock_build_challenge_interaction_response_ex
  -> response WT 1/2/2 { moveinfo } + WT 1/4/5 { side, battleinfo }
     [+ WT 1/2/1 empty movement acknowledgement]
  -> JianghuOL.CBE net dispatch
  -> mmBattle HandleServerBattleCmd(0x7BD0), subtype 5
  -> mmBattle HandleBattleStartMsg(0x66CC)
  -> first battle render
  -> JianghuOL.CBE:0x01004EA8 dereferences unit visual context +0x0C
```

`HandleBattleStartMsg` reads the subtype-5 `battleinfo` prefix as:

```text
u8 leftCount
repeat leftCount: u32 sceneIndex, u32 sceneX, u32 sceneY
u8 rightCount
repeat rightCount: role vitals/template fields
```

For each left monster it first tests `sceneIndex`, then scans the 25 live scene
nodes for an active kind-2 node whose battle coordinates at `node+240/+244`
equal `sceneX/sceneY`.  If neither matches, this client build exits the scan at
row 25 but continues copying that out-of-range row into the battle unit.  The
resulting unit has a null visual resource pointer and crashes on its first draw.

## First divergence

The challenge request already carries the client's live scene-node
`index/posx/posy`.  The current service copies those values, then overwrites all
three with `vm_net_mock_select_scene_actor_moveinfo_target()`.  In service-only
mode that selector reparses the SCE and chooses the first record with the same
actor id.  SCE record identity/order is not the same contract as the client's
current 25-row live scene table, especially when a monster actor id occurs more
than once.

The overwrite was introduced to accommodate `1/2/2` actor lookup: the client
movement handler locates a scene node by actor id.  It does not establish the
assumed battle-coordinate contract, however.  Its outer `x/y` fields update
`node+24/+26`, whereas subtype-5 battle matching reads `node+240/+244`.
Consequently the preceding movement object cannot make an arbitrary SCE-derived
coordinate valid for `HandleBattleStartMsg`.

First invalid state: the service emits subtype-5 `sceneIndex/sceneX/sceneY`
from an offline SCE lookup instead of the collision request's live-node tuple.
The client cannot resolve that tuple and constructs a battle unit from row 25.

## Evidence and negative evidence

- Runtime crash: response length 191, fault PC `0x01004EA8`, left battle-unit
  render coordinate `(40,140)`, null visual context at object `+0x0C`.
- IDA: `JianghuOL.CBE:0x01004EA8` is the `LDR [unitRenderObject,#0x0C]`
  dereference in the battle actor drawing path.
- IDA: `mmBattle:0x66CC` validates/scans the client live scene table using the
  subtype-5 index and `node+240/+244`, with no not-found guard before copying.
- Existing runtime in `2026-06-25-battle-server-flow.md` recorded the service
  replacing request tuple `index=2,pos=(87,179)` with SCE tuple
  `index=1,pos=(142,310)`.
- The `1/2/2` movement seed only proves HP/MP initialization by actor id.  It
  does not prove that changing the subtype-5 live-node tuple is valid.
- No evidence currently implicates the optional trailing `1/2/1` object; it is
  after the first wrong tuple and is not changed by this investigation.

## Planned correction and verification

For a normal scene-collision `1/4/1`, keep the request's exact live-node
`index/posx/posy` in both the subtype-5 battle start and its associated trace.
Do not replace it with a reparsed SCE row.  SCE selection remains appropriate
for autonomous/hangup battle creation, where there is no client-selected live
node tuple, and non-scene instance challenges continue to use subtype 10.

Implemented in `vm_net_mock_build_challenge_interaction_response_ex`: the
normal challenge path now preserves the parsed request tuple and reports
`target_source=request-live-node`.  The SCE selector and its existing callers
for autonomous battle creation are unchanged.

Verification must cover:

- the response log shows `target_source=request-live-node` and identical
  selected/request tuples;
- the client enters battle and draws the touched monster without a null-resource
  fault;
- the monster HP/MP template remains populated through the existing movement
  seed;
- escape/settlement returns to the scene normally;
- hangup and non-scene instance challenge paths retain their existing target
  sources and battle subtypes.

## Verification results

- `make -j2`: passed on 2026-07-22.
- The rebuilt service was restarted on `127.0.0.1:19090` with the admin endpoint
  on `127.0.0.1:19091`.
- A framed service request using the real 60-byte challenge grammar was sent
  with `id=105,index=2,posx=87,posy=179`.  It returned a 185-byte response whose
  first objects are the expected `1/2/2` movement seed and `1/4/5` battle start.
- The rebuilt service recorded:

```text
mock_challenge_battle_start ... subtype=5 scene_start=1
  index=2 pos=(87,179) req_index=2 req_pos=(87,179)
  target_source=request-live-node ... objects=2
net_send ... wt=4/1 len=60 source=builtin-challenge-interaction resp=185
```

This directly verifies that the first invalid state from the old path—the SCE
tuple overwrite—no longer occurs.  The autonomous hangup builder still calls
`vm_net_mock_select_scene_actor_moveinfo_target()`, and the dynamic-NPC instance
challenge continues to force non-scene subtype 10; neither adjacent contract
was changed.
