# New-role first-scene WT object cap

## Symptom

A newly selected role could enter its initial scene and then show the generic
`解包错误` message.  Runtime responses in this path were around 724--746 bytes.

## Client contract

`江湖OL.CBE:0x01012E4C` calls `event_packet_init(packet, 10, 19)` for event-7
business responses.  `event_packet_parse_WT(0x0103467A)` returns error 3 when
more than ten response objects are present.

The local-server reproduction for role 10027 in `c00蓬莱仙岛_01` showed the
first scene resource response contained 11 objects.  The requested startup
objects plus NPC catalog and scene completion already occupied all ten slots;
the same-response welcome/system chat object became the eleventh.

## Server contract

Scene-ready chat delivery is now budgeted by the remaining slots in the
ten-object response.  The session is still promoted to scene-ready immediately,
so the welcome message remains queued and is delivered by the next existing
scene-sync poll together with task-prompt refresh data.

## Validation

- Before: first-scene response `objects=11 resp=724`.
- After: first-scene response `objects=10 resp=662`.
- The next poll returned `objects=3 resp=176` (two task-prompt objects plus the
  deferred system message).
- A 44-second real-client run against `127.0.0.1:19090` selected role 10027,
  entered the scene, rendered the player/NPC nodes, and exited normally without
  an unpack-error popup or assertion.

