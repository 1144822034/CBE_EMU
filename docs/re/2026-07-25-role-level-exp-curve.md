# 2026-07-25 Role Level EXP Curve

## Goal

Server-owned level curve for title rows, scene actor info, login state, and
battle settlement:

- `1→2 = 200`
- `49→50 = 2_000_000`
- total EXP to reach level 70 = `120_000_000`
- consecutive upgrade costs grow by at most ~22% (hard cap intent ≤50%)
- no phase-boundary jumps between novice / growth / long-term bands
- `level` is still derived from persisted total EXP; `lastexp` / `curexp` /
  `persentexp` keep the existing wire contract

## Why not three pure arithmetic stages

An arithmetic growth band that opens at `100_000` after a `~20_000` novice end
creates a `~5×` jump at `30→31`, and `a=d=100_000` itself starts at `+100%`.
Those violate the smooth-transition requirement.

`69→70 ≈ 11_500_000` with stage-3 open at `2_100_000` also needs about
`136_000_000` for stage 3 alone, which exceeds the `120_000_000` total budget.

## Final calibrated curve

| Band | Upgrades | Shape | Growth | Band sum |
|------|----------|-------|--------|----------|
| 平滑抬升 | `1→2` .. `49→50` (49) | geometric table, `r ≈ 1.2115` | ~`+21%` / level | `11_454_020` |
| 长线养成 | `50→51` .. `69→70` (20) | arithmetic `a=2_100_000`, `d=350_242` | open `+5%`, then `~+4–17%` falling | `108_545_980` |

Geometric terms are stored as an exact integer table (rounded then nudged) so
the band sum and the `49→50` anchor stay bit-exact without depending on host
`double` rounding.

## Key nodes

| Upgrade | Cost | Step vs previous |
|---------|------|------------------|
| `1→2` | 200 | — |
| `2→3` | 240 | `+20.0%` |
| `29→30` | 43_088 | `+21.2%` |
| `30→31` | 52_202 | `+21.2%` |
| `48→49` | 1_650_807 | `+21.2%` |
| `49→50` | 2_000_000 | `+21.2%` |
| `50→51` | 2_100_000 | `+5.0%` |
| `69→70` | 8_754_598 | `+4.2%` |
| total to 70 | 120_000_000 | `level_start_exp(70)` |

## Formula

```text
level <= 49:  g_vm_net_mock_role_level_up_cost_geom[level - 1]
level >= 50:  2100000 + (level - 50) * 350242
```

`level_start_exp(level)` = sum of `level_up_cost(1..level-1)`.

`level_from_exp(exp)` caps at `VM_NET_MOCK_ROLE_MAX_LEVEL` (70). At max level,
`next_level_start_exp` returns `0xffffffff` so `persentexp` reports `100`.

## Implementation

Constants in `src/server/mock_server_core.c`:

```text
VM_NET_MOCK_ROLE_MAX_LEVEL
VM_NET_MOCK_ROLE_EXP_TOTAL_TO_MAX
VM_NET_MOCK_ROLE_EXP_GEOM_LAST_LEVEL
VM_NET_MOCK_ROLE_EXP_STAGE3_A / _D / _FIRST_LEVEL
```

Helpers / table in `src/server/mock_server_role.c`:

```text
g_vm_net_mock_role_level_up_cost_geom[49]
vm_net_mock_role_level_up_cost()
vm_net_mock_role_level_start_exp()
vm_net_mock_role_level_from_exp()
vm_net_mock_role_last_level_exp()
vm_net_mock_role_next_level_start_exp()
vm_net_mock_role_exp_percent()
```

Wire field semantics remain as in `docs/re/2026-06-27-role-level-scaling.md`.

## Validation

```text
make -j2
```

Numeric checks:

```text
level_up_cost(1)  = 200
level_up_cost(49) = 2000000
level_up_cost(50) = 2100000
level_up_cost(69) = 8754598
level_start_exp(70) = 120000000
max consecutive cost ratio over 1..69 ≈ 1.217
```
