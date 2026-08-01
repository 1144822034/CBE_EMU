# 2026-06-27 Role Level Scaling

## Goal

Use one server-side level model for title role rows, scene actor info, login
state, and battle settlement:

- new roles start at level 1 with 0 total EXP
- total EXP remains the persisted source of truth; `level` is normalized from it
- the numeric curve itself was replaced on 2026-07-25; see
  `docs/re/2026-07-25-role-level-exp-curve.md` for the three-stage arithmetic
  parameters (`1→2=200`, `49→50=2_000_000`, total to 70 = `120_000_000`)

## Formula

Historical triangular helper (removed):

```text
levelStartExp(level) = 100 * (level - 1) * level / 2
nextLevelCost(level) = level * 100
```

Current curve helpers and constants are documented in
`docs/re/2026-07-25-role-level-exp-curve.md`.
## Wire Fields

`mmBattleMstarWqvga.cbm:0x743C` only proves the battle settlement parser reads
these fields in order; the actual progression rule is server-owned:

```text
exp, lastexp, curexp, persentexp, energy, energymax, gold, level, ...
```

The mock-server sends:

```text
exp         reward EXP gained by this settlement
lastexp     total EXP at the current level's start threshold
curexp      total EXP at the next level's start threshold
persentexp  current-level progress percentage, still encoded as an integer field
level       level derived from total persisted EXP
```

The property UI does not render this as a single string. Main CBE
`InitSceneViewport(0x010221EE)` fills the 20-row value table at
`R9+0x6390`:

```text
0x010226E..0x0102284  copy actor+0xB0 total EXP into scene+0x4EC
0x01022302            row 8:  fmt "%d/" with scene+0x4EC - lastexp
0x01022314            row 9:  fmt "%d"  with curexp - lastexp
```

`persentexp` is parsed into a separate halfword progress cache at
`scene+0x4FC`; the property page does not use it as the denominator.

For example, after a role reaches 310 total EXP:

```text
level = 3
lastexp = 300
curexp = 600
persentexp = 3
shown as 10/300
```

For a fresh level-1 role:

```text
total persisted EXP = 0
level = 1
lastexp = 0
curexp = 100
persentexp = 0
shown as 0/100
```

## Implementation

`src/mock-server.c` now centralizes the rule in:

```text
vm_net_mock_role_level_start_exp()
vm_net_mock_role_level_from_exp()
vm_net_mock_role_last_level_exp()
vm_net_mock_role_next_level_start_exp()
vm_net_mock_role_exp_percent()
```

`vm_net_mock_role_normalize()` continues to rewrite the persisted `level` from
`exp`, so old rows and newly created rows use the same rule after load.

`role->exp` remains the persisted total EXP. Login actorinfo `actor+176`
(`actor+0xB0`) sends that total value. `lastexp` and `curexp` send the
surrounding level brackets so the property page can render current-level
progress. A negative runtime result (`-100/-100`) proved that sending
`lastexp=100, curexp=0, persentexp=0` was wrong because the client subtracts
`lastexp` from both property display values. A later `0/0` regression proved
that sending total EXP as `curexp` is also wrong: the property denominator reads
`curexp - lastexp`, while current EXP comes from `actor+0xB0 - lastexp`.

The named `persentexp` field must be encoded as a normal integer/u32 object
field, just like `lastexp` and `curexp`. IDA evidence:

```text
JianghuOL.CBE scene_runtime_init_and_sync:
  0x01013482 lastexp     via object getter +0x44
  0x01013490 curexp      via object getter +0x44
  0x0101349E persentexp  via object getter +0x44, then STRH to UI cache

mmBattleMstarWqvga.cbm HandleBattleSettleMsg:
  0x7486 lastexp     via object getter +0x44
  0x749C curexp      via object getter +0x44
  0x74B2 persentexp  via object getter +0x44, then STRH to UI cache
```

The final `STRH` is only the client's internal display cache width; it is not
the wire/object field type. Runtime regression evidence: sending `persentexp`
as a u16 field on login left the parsed progress cache at `0` because the
client reads it through the integer getter before narrowing it.

Do not move the EXP bracket values into the early scalar slots of
scene/login `actorinfo`: a runtime probe that sent the next-level threshold in
the third scalar changed `scene_probe_node` HP from `120/120` to `120/100`,
proving that slot still feeds scene HP max on fresh map initialization.

## Validation

Validated with:

```text
make
git diff --check
cd bin && .\main.exe --autotest --shot-ms=5000 --max-ms=52000 --actions=5000:key:f,17000:key:f,19000:key:q,23000:key:f,29000:key:f,35000:key:f
```

The normal `make` completed successfully. `git diff --check` only reported Git
CRLF conversion warnings for touched text files; no whitespace errors were
reported. The longer autotest path reached a ready scene and exited through
`autotest_exit` without an assert; `scene_probe_node[0]` stayed at
`battleHp=120/120`, confirming the failed actorinfo EXP-slot probe was not left
in the runtime path.

Follow-up validation after the u32 `persentexp` correction:

```text
make
cd bin && .\main.exe --autotest --shot-ms=5000 --max-ms=52000 --actions=5000:key:f,17000:key:f,19000:key:q,23000:key:f,29000:key:f,35000:key:f
```

The path enters the map and exits via `autotest_exit`. Attribute-page opening is
left for manual operation; the runtime probe only records the property table
when the user opens it.

Follow-up validation after the property-page EXP split correction:

```text
make
cd bin && .\main.exe --autotest --shot-ms=5000 --max-ms=52000 --actions=5000:key:f,17000:key:f,19000:key:q,23000:key:f,29000:key:f,35000:key:f
```

The enter-map path completed without assertion or communication stalls. The
attribute page itself is intentionally left for manual opening.
