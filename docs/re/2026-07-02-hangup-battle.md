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
- `1/2/2`: moveinfo for the selected scene monster node.
- `1/4/5`: battle start, using the normal scene-monster battle start blob.
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
- The standalone service cannot read the emulator process's `R9+0x5CB0` scene
  node table. It resolves the selected monster instance from the real SCE2
  scene resource instead. The recovered combat-spawn record is:

```text
u16 kind = 3
u16 x
u16 y
meta token 5/6: field 14 = actor id
string field 15 = display name
scalar field 16 = visual/class hint
string field 17 = actor resource
```

- The response first sends `2/2 moveinfo` for this actor id and coordinate.
  `HandleBattleStartMsg(0x66CC)` checks the supplied row index, then scans up to
  25 active kind-2 scene rows for matching coordinates. This keeps the SCE
  fallback valid when dynamic actors have shifted the live slot ordinal.

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
- After adding the real-SCE combat-spawn fallback, the 24-byte request returns
  `2/10 + 2/2 + 4/5 + 4/11` (`248` bytes in the regression scene).
- The 52-byte request with a trailing movement upload returns the same battle
  objects plus empty `2/1` ACK (`254` bytes).
- Both requests log as `source=builtin-hangup-battle-start`, include
  `mock_scene_monster_target ... source=SCE2-combat-spawn`, leave the service
  alive, and produce zero stderr bytes.
