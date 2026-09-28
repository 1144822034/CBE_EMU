# 2026-07-25 Role Level EXP Curve

## Goal

Server-owned level curve for title rows, scene actor info, login state, and
battle settlement.

**2026-09-28 recalibration** — decade upgrade anchors supplied by design:

- `1→2 = 200` (novice open, retained)
- `29→30 = 5_368`
- `39→40 = 85_800`
- `49→50 = 1_040_000`
- `59→60 = 11_704_000`
- `69→70 = 100_100_000`
- total EXP to reach level 70 = `511_943_065`
- between anchors: piecewise geometric (integer-rounded, ends exact)
- max consecutive cost growth ≈ `+32%` (within historical ≤50% hard-cap intent)
- `level` is still derived from persisted total EXP; `lastexp` / `curexp` /
  `persentexp` keep the existing wire contract

## Why not the prior geom(1–49)+arith(50+) curve

The previous lock (`49→50 = 2_000_000`, `69→70 ≈ 8_754_598`, total `120_000_000`)
does not match the new decade anchors. High-band costs are now much steeper
(`69→70 = 100_100_000`), so stage-3 arithmetic cannot fit.

## Final calibrated curve

| Band | Upgrades | Shape | ~Growth / level | Notes |
|------|----------|-------|-----------------|-------|
| 新手抬升 | `1→2` .. `29→30` | geometric | `~+12.5%` | open `200` → `5_368` |
| 30 段 | `30→31` .. `39→40` | geometric | `~+31.9%` | → `85_800` |
| 40 段 | `40→41` .. `49→50` | geometric | `~+28.3%` | → `1_040_000` |
| 50 段 | `50→51` .. `59→60` | geometric | `~+27.4%` | → `11_704_000` |
| 60 段 | `60→61` .. `69→70` | geometric | `~+23.9%` | → `100_100_000` |

All 69 upgrade costs are stored as one exact integer table so host `double`
rounding cannot drift the anchors or the total.

## Key nodes

| Upgrade | Cost | Step vs previous |
|---------|------|------------------|
| `1→2` | 200 | — |
| `29→30` | 5_368 | `~+12.5%` band |
| `39→40` | 85_800 | — |
| `49→50` | 1_040_000 | — |
| `59→60` | 11_704_000 | — |
| `69→70` | 100_100_000 | — |
| total to 70 | 511_943_065 | `level_start_exp(70)` |

## Formula

```text
level_up_cost(level) = g_vm_net_mock_role_level_up_cost_table[level - 1]
  for level in 1..69
```

`level_start_exp(level)` = sum of `level_up_cost(1..level-1)`.

`level_from_exp(exp)` caps at `VM_NET_MOCK_ROLE_MAX_LEVEL` (70). At max level,
`next_level_start_exp` returns `0xffffffff` so `persentexp` reports `100`.

## Implementation

Constants in `src/server/mock_server_core.c`:

```text
VM_NET_MOCK_ROLE_MAX_LEVEL
VM_NET_MOCK_ROLE_EXP_TOTAL_TO_MAX   /* 511943065 */
```

Helpers / table in `src/server/mock_server_role.c`:

```text
g_vm_net_mock_role_level_up_cost_table[69]
vm_net_mock_role_level_up_cost()
vm_net_mock_role_level_start_exp()
vm_net_mock_role_level_from_exp()
vm_net_mock_role_last_level_exp()
vm_net_mock_role_next_level_start_exp()
vm_net_mock_role_exp_percent()
```

Client mirror: `PixiJS_web/src/game/roleLevelExp.ts`.

Wire field semantics remain as in `docs/re/2026-06-27-role-level-scaling.md`.

## Migration note

Persisted total EXP is unchanged; `level` is re-derived on load/gain.
Characters previously near the old 120M cap (~level 70) land around level 63
on this curve until they earn more EXP.

## Validation

```text
make -j2
```

Numeric checks:

```text
level_up_cost(1)  = 200
level_up_cost(29) = 5368
level_up_cost(39) = 85800
level_up_cost(49) = 1040000
level_up_cost(59) = 11704000
level_up_cost(69) = 100100000
level_start_exp(70) = 511943065
```
