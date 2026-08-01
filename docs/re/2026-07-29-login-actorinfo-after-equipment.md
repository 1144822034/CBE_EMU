# 2026-07-29 login actorinfo after equipment

## Status

**Reverted.** Appending full-blob `1/1/14` after `equipment_login` is incorrect.

`parse_actorinfo_response(a2!=0)` skips several field **reads** (base HP/MP,
EXTRA132, some strings) without advancing past those wire words. A login-shaped
full `actorinfo` therefore desyncs on refresh.

Login property authority remains subtype-6 `actorinfo` + wear apply.
Map/battle vitals may still use `1/1/14` under the live-update field subset.

物攻 mapping fix: `2026-07-29-actorinfo-attack-word-mapping.md`.
