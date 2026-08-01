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

Success response when a live scene-node tuple is known:

- `1/2/10`: empty actor-other acknowledgement.
- `1/2/2`: moveinfo for the selected scene monster node.
- `1/4/5`: battle start, using the normal scene-monster battle start blob.
- Optional `1/4/11`: auto-battle UI flag, controlled by `CBE_HANGUP_BATTLE_AUTO_FLAG`.
- When the request contains the trailing movement upload, one empty `1/2/1`
  acknowledgement is appended after the battle objects.

Success response when no live scene-node tuple is known (android/service-only):

- `1/2/10`: empty actor-other acknowledgement.
- `1/4/10`: non-scene battle start with embedded enemy/role templates.
- Optional `1/4/11`: same auto-battle UI flag.
- Optional trailing empty `1/2/1` when the request flushed movement.

Failure response:

- `1/2/10`: empty actor-other acknowledgement.
- `1/25/11`: `result = 8`, `info = "No hangup monster"`.
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
- Subtype-5 battle start requires the client's live 25-row scene-node tuple
  (`index` + battle coords at `node+240/+244`).  The standalone service cannot
  read `R9+0x5CB0`, so it must not emit SCE combat-spawn ordinals as that
  tuple.  Hangup now resolves the tuple from:
  1. the session's last successful scene-monster `4/1` challenge live-node; or
  2. an in-process emulator live-table scan (no SCE fallback).
  If neither is available, hangup falls back to non-scene `1/4/10` with an
  embedded enemy template (`target_source=non-scene-subtype10`).
- SCE2 combat-spawn records remain useful as human/admin catalog evidence, but
  their spawn ordinal is not a live scene index.  See
  `2026-07-25-hangup-battle-start-crash.md`.

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

## 2026-07-25 Crash Correction

- Client fault after hangup `resp=248`: `JianghuOL.CBE:0x01004EA8` null visual
  at battle-unit `+0x0C`, same as the 2026-07-22 scene-monster crash.
- Same-scene challenge had already proven the live tuple
  `index=6 pos=(102,287)`; hangup emitted SCE ordinal `index=1` for the same
  coordinates and crashed on first draw.
- Hangup no longer uses SCE spawn ordinals for subtype 5.  It reuses the
  session's last challenge live-node, or an emulator live scan, otherwise
  answers with non-scene `4/10`.  Details:
  `2026-07-25-hangup-battle-start-crash.md`.

## 2026-07-25 Hangup Loop

- After a hangup start with auto/`prefer`, victory marks `ScheduleAfterExit`;
  delayed `4/8` tear-down then arms map-side re-entry
  (`CBE_HANGUP_LOOP_INTERVAL_MS`, default 2000).  Scene poll delivers the next
  synthetic hangup start while prefer remains set.
- Explicit auto off, escape, death, or scene change clears the loop.
- See `2026-07-28-hangup-loop-pacing-refactor.md`.
- See `2026-07-25-hangup-loop-15s.md`。
