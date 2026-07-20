# Login-scene nearby-player baseline

## Symptom

After role selection, an already idle player in the same scene was absent from
the new client's scene.  The node appeared only after a movement upload caused
the normal scene-sync poll to emit movement data.

## Client contract

- `1/2/10 otherinfo` is consumed by `sub_1012958`, which calls
  `scene_node_find_or_create` and supplies the row's authoritative coordinates.
- `1/2/2 moveinfo` is consumed separately by `net_handle_actor_move_info` case
  2 and arms the per-frame direction queue.
- `scene_runtime_init_and_sync` (`0x01012FB4`) emits the resource/task follow-up
  only after the map and actor scene state exist, so the response that already
  carries the initial NPC catalog and welcome message is a valid node-creation
  window.

## Server cause and fix

`vm_net_mock_build_scene_resource_followup_response` promoted the service
session to scene-ready and delivered NPC/chat data, but it did not include the
same-scene role baseline.  The full nearby helper was only used by the transfer
task-subset path and by later polls.

Both startup builders now fill the request's existing `1/2/10` response slot
with every currently visible same-scene role (up to the existing nearby cap)
at its current position.  They no longer append a second `1/2/10` object.  For
each row the observer's service-side peer cursor is marked visible and
baselined to the source's current movement serial.

The startup completion helper performs one final pending-to-ready lifecycle
transition.  Because that transition deliberately clears observer visibility,
the cursor baseline is restored once more after completion; the next poll does
not redundantly resend the same startup rows.

No `1/2/2` history is attached to this startup baseline.  This prevents an old
direction queue from replaying after node creation.  The next newly uploaded
movement serial is still emitted exactly once by the normal scene-sync poll.
The object count does not grow.  This matters for the full 54-byte startup
request: skill, book, backpack, NPC, task and chat data already consume all ten
safe response slots.

Runtime negative evidence from return-title relogging showed the old
task-subset path produce `objects=12 resp=746`: the original empty `2/10`, a
second non-empty `2/10`, one `2/2`, and chat all occupied separate objects.
The client displayed an unpack-error prompt.  Reusing the original slot keeps
that same response at ten objects and omits the historical `2/2`.

An observer already present in the scene continues to discover a newly ready
peer through the existing per-frame service poll, without requiring either
player to move.

## Verification

`tmp/login-scene-nearby-regression.php` creates two temporary MySQL accounts in
`c00蓬莱仙岛_03`, performs the real 54-byte skill/book/backpack/task startup
request for both, and sends no movement request.  It checks that:

1. client B's scene-completion response contains client A in non-empty
   `1/2/10 otherinfo`;
2. that startup response contains no `1/2/2` movement replay;
3. client A's next scene poll contains client B's baseline.
4. client B's next poll does not repeat either `1/2/10` or `1/2/2`.
5. returning client B to login on the same service client id and selecting the
   role again still returns exactly one `1/2/10`, no `1/2/2`, and no more than
   ten objects.

The fixture disconnects both service sessions and deletes its MySQL rows in a
`finally` block.  Because the service loads the account catalog at startup, a
full cold-start validation can pre-create the fixture rows, restart the
service, and run with `CBE_TEST_NEARBY_PRESEEDED=1`; normal ad-hoc runs retain
the self-seeding path for services that support live account reload.

## Evidence record: return-title scene startup

```text
phase: return-title relog -> current-scene task subset
status: validated

request:
  wt_kind: 12
  wt_subtype: 1
  objects: 12/1, 7/42, 17/1, 6/1, 6/13, 6/14, 2/10, 25/5
  key_fields: 2/10 Type=101
  sample_len: 54
  packet_log: mock-service-19090.login-nearby-final.20260720-172143.stdout.log

response:
  objects: 27/11 NPC catalog; 12/1; 7/42; 17/1; 2/10; 6/1; 6/13; 6/14; 25/5; 3/3 chat
  fields: 2/10 othernum
  arrays: 2/10 otherinfo contains same-scene role rows and current coordinates
  blobs: no startup 2/2 movement history

ida_evidence:
  binary: 江湖OL.CBE
  function: sub_1012958; scene_runtime_init_and_sync(0x01012FB4)
  dispatch_case: 1/2/10 actor otherinfo
  parser_reads: othernum, otherinfo row identity/appearance/coordinates
  failure_branch: practical first-scene dispatch limit is ten response objects

runtime_evidence:
  trace_lines: old objects=12 resp=746; fixed objects=10 resp=549
  handled_source: builtin-scene-task-subset-followup
  queued_event: event 7
  client_effect: startup response creates idle peer without a movement upload

negative_evidence:
  missing_or_bad_field: duplicate 2/10 plus startup 2/2 exceeded the object limit
  observed_failure: return-title relog displayed an unpack-error prompt

unknowns:
  - name: exact UI error branch address
    current_value: not required for packet correction
    why_kept: runtime object-limit evidence and the parser contract are sufficient
```
