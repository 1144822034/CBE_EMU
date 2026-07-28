# Hangup Battle Request

## Runtime Signature

- Request WT: `2/10`
- Objects:
  - `1/2/10` with `Type = 2`, payload length `10`
  - empty `1/25/3`
  - The live scene button may append one `1/2/1` object whose `moveinfo`
    field is the pending ten-byte direction timeline (object payload length
    `23`). This is a movement-queue flush in the same WT packet, not a second
    hangup marker.
- Observed failure before handling:
  - `unhandled wt=2/10 len=24 objects=1 first=1/2/10:10,1/25/3:0`
  - `unhandled wt=2/10 len=52 objects=1 first=1/2/10:10,1/25/3:0,1/2/1:23`

## IDA Evidence

- `JianghuOL.CBE:0x01015E14` (`HandleBattleEnterReq`) builds the outgoing `2/10` game event and writes `Type = 2`.
- `JianghuOL.CBE:0x01012E4D` dispatches business response subtype `25`.
- `JianghuOL.CBE:0x01010C7E` consumes response-side:
  - `25/11`: `result = 8`, then `info` string for the center banner state.
  - `25/12`: `result = 4`, then clears the banner state.
- No response-side `25/3` parser was found. The request marker must not be echoed.
- `JianghuOL.CBE:0x01012ADC` dispatches response `2/1`; its subtype-1 branch
  does not read fields. Therefore the appended movement upload is answered by
  the normal empty `2/1` acknowledgement.
- `mmBattle:0x66CC` consumes battle-start `4/5` with `side` and `battleinfo`.

## Server Contract

Success response:

- `1/2/10`: empty actor-other acknowledgement.
- `1/2/2`: the selected scene monster's HP/MP seed.
- `1/4/5`: scene-monster battle start. Its `battleinfo` contains the server
  SCE2 combat-spawn tuple and local player vitals; the client copies the
  monster model from its existing scene node.
- Optional `1/4/11`: auto-battle UI flag, controlled by `CBE_HANGUP_BATTLE_AUTO_FLAG`.
- When the request contains the trailing movement upload, one empty `1/2/1`
  acknowledgement is appended after the battle objects.

Failure response:

- `1/2/10`: empty actor-other acknowledgement.
- `1/25/11`: `result = 8`, `info = "No hangup monster"` or `"Monster not ready"`.
- When present, the trailing movement upload is still consumed through the
  existing movement handler and receives the same empty `1/2/1`
  acknowledgement. Its position/session side effects are not skipped merely
  because no battle target is ready.

## Data Source

- The server chooses the hangup monster from `automonster.dsh`.
- Load order:
  - `JHOnlineData/automonster.dsh`
  - `bin/JHOnlineData/automonster.dsh`
  - `web/fs/JHOnlineData/automonster.dsh`
- Matching uses loose scene-name comparison, then chooses one of the row monster ids.
- `CBE_HANGUP_BATTLE_ENEMY_ID` can force a monster id for debugging only.
- The service chooses the monster **type** from `automonster.dsh` and selects
  its first matching SCE2 combat spawn from the server-owned scene resource.
  `HandleBattleStartMsg(0x66CC)` resolves that source tuple by coordinate if
  its SCE ordinal differs from the client's live node slot.

## Implementation Notes

- Handler source: `src/mock-server.c`, `builtin-hangup-battle-start`.
- This is intentionally narrower than generic `2/10`: it requires the exact
  `Type = 2` plus empty `25/3` signature, followed by either no object or
  exactly one valid ten-direction `2/1 moveinfo` upload. Other trailing objects
  remain unhandled.
- Do not add JSON fallback or client-global reads for this feature. The server must answer from server-side scene and `automonster.dsh` data.

## 2026-07-20 Regression

- First crash fix: replaying the exact 52-byte three-object request returned a
  bounded failure response instead of reaching the unhandled assertion.
- Follow-up stall evidence: the real 24-byte button request returned
  `2/10 + 25/11 "Monster not ready"`. `HandleBattleEnterReq(0x01015E14)` had
  already set the client battle state to `3`, while the banner parser did not
  reset it, leaving the UI at `获取数据`.

### 2026-07-28 visual-contract correction

The temporary `4/10` path avoided the old null scene-node draw, but user
runtime then showed a player-shaped opponent on the left and a player attacking
that opponent. This is an earlier protocol deviation, not a rendering bug.

`mmBattle:HandleBattleStartMsg(0x66CC)` gives subtype `4/10` one full left row
with two visual bytes and a short right row resolved from the local party
template. The same full-row construction is used by the verified duel builder,
where those two bytes are the peer's job and sex codes. They are not an
arbitrary `.actor` resource field. The hangup builder supplied generic `0/1`,
which the client faithfully decoded as a player appearance; `4/10` therefore
cannot represent an `e_mucusP.actor` monster.

The server/source resource contract is now sufficient for a real scene start:

- `bin/JHOnlineData/01桃花岛_01.sce` and
  `web/fs/JHOnlineData/01桃花岛_01.sce` share SHA-256
  `AE11796E2970FF97A5CBBC855F7E1141A75E8D96E79B16DC9A2CDF402863FCEE`.
- Decoding that common SCE2 resource produces four `actor_id=105` combat
  spawns: `(295,57)`, `(179,120)`, `(146,349)`, `(292,484)`; each has
  `e_mucusP.actor`.
- In the same runtime, normal collision battles successfully sent live tuples
  `(index=6,pos=(295,57))` and `(index=8,pos=(146,349))`. Thus the source
  resource's coordinates are present in the live client node table.
- `0x66CC` first tests its supplied index then scans active kind-2 nodes by
  `node+240/+244` coordinates. The SCE ordinal need not equal the live slot.
- `scene_node_update_move_blob(0x01012A76)` seeds HP/MP at the first active
  actor-id match. The hangup selector also chooses the first matching SCE
  combat spawn, so its preceding `2/2` and the `4/5` source refer to the same
  monster node.

The corrected response is:

```text
1/2/10 { othernum=0, otherinfo="" }
1/2/2  { moveinfo for first verified SCE combat spawn }
1/4/5  { side=1, battleinfo(scene-index, scene-x, scene-y, player vitals) }
1/4/11 { result=1, type=1 }                 # if auto is enabled
[1/2/1 empty acknowledgement]               # only when uploaded with request
```

There is no `4/10` fallback for scene hangup. If the selected
`automonster.dsh` id has no corresponding server SCE2 combat spawn, the handler
returns `2/10 + 25/11` instead of fabricating a player-template battle.

Expected trace:

```text
mock_hangup_battle_start ... subtype=5 index=<sce ordinal> pos=(<x>,<y>)
  target_source=sce-combat-spawn-coordinate
  ... response=2/10+2/2+4/5+4/11[+2/1]
```

Manual regression required:

1. On 桃花岛_01, press 挂机 twice. Both entries show 毒泥怪 on the left and
   the local role on the right; no player-shaped opponent appears.
2. Automatic solo actions use scene slots (`actor=1,target=0`) and the result
   panel returns to the scene normally.
3. Repeat within the eight-second reward cooldown. The visual contract remains
   subtype 5, while terminal closure follows the separate no-reward
   `4/11 + 4/9` poll contract.
4. A configured hangup monster with no server SCE2 spawn shows the bounded
   `Monster scene node unavailable` banner rather than entering battle.
