# 2026-07-29 actorinfo bare primary attributes

## Status

Superseded for the six property words by
`2026-07-29-login-actorinfo-after-equipment.md`.

Bare seeding alone did not fix login property panel: subtype-6 `actorinfo`
arrives before `7/7 type=2` wear rows, and the panel needs a post-equipment
`1/1/14` refresh with **full** `build_player_stats` totals.

HP/MP `primaryBaseMax` / `secondaryBaseMax` remain bare (client re-add path
at `+0xBC` → `+0xC4` is still proven).
