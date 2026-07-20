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

The startup resource response now appends one compact non-empty `1/2/10`
object after scene readiness.  It contains every currently visible same-scene
role (up to the existing nearby cap) at its current position.  For each row the
observer's service-side peer cursor is marked visible and baselined to the
source's current movement serial.

The startup completion helper performs one final pending-to-ready lifecycle
transition.  Because that transition deliberately clears observer visibility,
the cursor baseline is restored once more after completion; the next poll does
not redundantly resend the same startup rows.

No `1/2/2` history is attached to this startup baseline.  This prevents an old
direction queue from replaying after node creation.  The next newly uploaded
movement serial is still emitted exactly once by the normal scene-sync poll.
The first-scene response grows by only one WT object and remains below the
confirmed ten-object practical limit.

An observer already present in the scene continues to discover a newly ready
peer through the existing per-frame service poll, without requiring either
player to move.

## Verification

`tmp/login-scene-nearby-regression.php` creates two temporary MySQL accounts in
the same scene, performs login/role selection for both, and sends no movement
request.  It checks that:

1. client B's scene-completion response contains client A in non-empty
   `1/2/10 otherinfo`;
2. that startup response contains no `1/2/2` movement replay;
3. client A's next scene poll contains client B's baseline.
4. client B's next poll does not repeat either `1/2/10` or `1/2/2`.

The fixture disconnects both service sessions and deletes its MySQL rows in a
`finally` block.  Because the service loads the account catalog at startup, a
full cold-start validation can pre-create the fixture rows, restart the
service, and run with `CBE_TEST_NEARBY_PRESEEDED=1`; normal ad-hoc runs retain
the self-seeding path for services that support live account reload.
