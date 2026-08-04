# 2026-07-29 login actorinfo after equipment

## Status

**Reverted.** Appending full-blob `1/1/14` after `equipment_login` is incorrect.

`parse_actorinfo_response(a2!=0)` skips several field **reads** without
advancing the stream: `summaryStatus`, base HP/MP maxima, money + backpack
bytes, target bytes, preview string, sceneKey + motion pair. **EXTRA132 and
the six property words are always consumed.** A login-shaped full `actorinfo`
therefore desyncs on refresh.

Login property authority remains subtype-6 `actorinfo` + wear apply.
Map/shop/equip `1/1/14` must use `build_actor_info_ex(..., liveRefresh=true)`.

物攻 mapping fix: `2026-07-29-actorinfo-attack-word-mapping.md`.
