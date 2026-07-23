# Compound WT Request and Response Dispatch

## Phase

Make independently serviceable WT request objects composable without allowing a
generic splitter to consume request families whose object ordering or state
transition is semantic.

## Reproduction / First Divergence

The mock receives one WT packet which can contain several request objects.  A
business handler often accepts the corresponding object only when it is the
whole packet.  The existing fallback `vm_net_mock_build_split_safe_combo_response`
works only for a fixed `vm_net_mock_object_is_split_safe` whitelist.  Therefore
an already implemented object outside that list causes the *whole* heterogeneous
packet to reach the unhandled assertion, despite being serviceable in isolation.

This is not a client parser limitation.  It is a server request-dispatch and
response-composition boundary.

## Client Evidence

- Binary: `江湖OL.CBE` (selected by binary name in IDA).
- `net_business_response_dispatch`, `0x01012E4C`:
  - `event_packet_init` parses the received event-7 WT packet into an entry
    array at `global+21912` with a count at `global+21904`.
  - The function loops over every entry (`88`-byte stride), dispatching each by
    its response kind; it does not stop after the first entry.
  - Thus a correctly framed WT response with multiple response objects is an
    intended client contract.  Object order remains observable and must be
    preserved unless a feature-specific parser documents another order.
- Existing feature evidence also uses multi-object responses: shop opening,
  scene follow-up, hangup battle, and backpack/books helpers call
  `vm_net_mock_append_response_objects` before `vm_net_mock_finish_wt_packet`.

## Existing Server Boundary

- Requests use `WT + object...` with the first object beginning at byte 4;
  `vm_net_mock_next_request_object` is the sole structural iterator.
- Responses use `WT + objectCount + object...`, and
  `vm_net_mock_append_response_objects` extracts response objects beginning at
  byte 5.
- The historical splitter recursively constructed one-object requests, but
  accepted a subresponse without checking its declared WT length or declared
  response-object count.  The current composer retains the explicit safe list
  and validates every nested response before merging it.
- Certain request combinations are deliberately atomic and must remain outside
  generic splitting.  For example, occupied-slot equipment replacement is
  `1/7/9 {body,bag}`, optionally followed by `1/2/10`; both encodings receive
  the dedicated `1/7/9 {result}` response from `builtin-item-equip-swap`.

## Contract for the Generic Path

The generic path applies only when all of the following are true:

1. The request has two or more structurally valid objects.
2. Every object is classified as independently composable.  The classification
   is explicit and is narrower than merely matching a WT kind/subtype.
3. No feature-specific multi-object handler matched first.
4. Every single-object builder produces a structurally valid response packet.

The composer appends each produced response object's original wire bytes in
request order, checks nested WT length/object-count contracts, then writes one
outer response count and length.  It never invents an acknowledgement for an
unhandled object.  If any prerequisite fails, the complete request remains
unhandled for feature-specific protocol investigation.

## Planned Change

1. Rename the former split-safe predicate to document the independent-combo
   contract and keep its explicit object signatures.  `1/7/42` is included as
   a read-only books/backpack refresh object; shop-specific `17/1 + 7/42`
   combinations still match their dedicated builders first.
2. Add strict response validation to the reusable append helper: outer WT
   length, declared object count, each embedded object length, 16-bit outer WT
   length limit, and destination capacity must all agree before bytes are
   copied.
3. Replace the recursive splitter with an all-or-nothing independent-combo
   composer: structural preflight first, then one-object handling and response
   object concatenation.
4. Add compact compound-only trace lines with request and response object
   counts.  No per-frame or client-state instrumentation is added.

## Implementation

- `vm_net_mock_build_independent_combo_response` runs after all dedicated
  multi-object builders.  It preserves request-object order when concatenating
  the validated single-object responses.
- `vm_net_mock_append_response_objects` now accepts only complete response
  packets made by `vm_net_mock_finish_wt_packet`; malformed nested packet bytes
  cannot be forwarded into a client event.
- The handled source is `builtin-independent-combo` (or
  `builtin-chat-message-independent-combo` when the chat-specific early route
  invokes the same composer).

## Validation

- Build with `make -j2`.
- Exercise an existing heterogeneous independent request combination and verify
  one event-7 response has the expected object count and each client parser
  runs.
- Re-run the dedicated occupied-equipment swap (`7/9 + 2/10`) to confirm the
  generic composer does not steal its atomic transaction.
- Verify malformed nested subresponses are rejected before their bytes reach a
  client event.

## 2026-07-23 Object-stream dispatch follow-up

Status: implemented and server-only validated

### Current limitation

The existing `vm_net_mock_build_independent_combo_response` already performs
structural splitting, recursively invokes the one-object route, and merges
valid response objects.  It is intentionally all-or-nothing: every request
object has to be in a narrow capability whitelist.  This prevents partial
side effects, but a harmless order change or an additional independently
serviceable object makes the complete packet miss the generic path.

The reported scene direct-enter repro exposed a second form of the same issue.
The live 44-byte request was:

```text
1/16/3 { exitID: typed-u32(current X), type: typed-u8(0) }
1/27/11 {}
1/7/42  {}
```

The original direct-enter handler required this exact packet length and exact
object order.  A shorter synthetic single-object test passed, but did not
represent the live request.  The full packet was then misrouted to the broad
teleport-stone detector until the exact 44-byte contract was added.

### Why a blanket splitter is not valid

The following are confirmed atomic request families; their objects share one
server-side state transition or one response contract and must not be sent to
unrelated one-object handlers:

| request family | evidence | reason it stays atomic |
| --- | --- | --- |
| `16/2 + 16/3 + optional item object` teleport confirmation | `mock_teleport_stone_confirmed_exit_combo` runtime logs | `16/2` establishes the confirmed exit; `16/3` consumes the same exit and defers exactly one scene entry. |
| `7/9 {body,bag} + optional 2/10` occupied-slot equipment replacement | `parse_item_equip_swap_request` | `2/10` is a permitted companion; the replacement result is one `7/9` contract. |
| `6/3 + 6/6` task progress/state submit | `mock_task_progress_state_combo` | both task IDs are validated before either persistent mutation is applied. |
| `16/3(type=0) + 27/11 + 7/42` direct scene-runtime follow-up | `江湖OL.CBE:0x01012FB4`, `0x01037998` | the runtime ACK establishes scene context; `27/11` must use that fresh scene shell and `7/42` is its immediately requested catalog. |

Conversely, IDA evidence from `江湖OL.CBE`
`net_business_response_dispatch(0x01012E4C)` shows that a normal event-7
response can contain multiple response objects: it parses an entry list and
dispatches every entry in wire order.  Therefore the valid generalization is
**object-stream composition**, not “one incoming packet always equals one
independent transaction.”

### Implementation contract

1. Every incoming WT will be structurally iterated as a request-object stream;
   request objects use their five-byte header and have no object-count byte.
2. Dedicated atomic builders retain priority and own only the objects whose
   relationship is documented above.
3. The generic composer may recursively route only objects that explicitly
   declare independent-composition support.  It applies mutations in request
   order and appends only fully validated subresponses in that same order.
4. Scene direct-enter will become a context-aware stream handler: it recognizes
   the `16/3 type=0` primary object, responds to whichever of `27/11` and
   `7/42` are actually present, and may append registered independent
   companions.  It must never infer a teleport exit from the runtime X value.
5. A stream containing an unknown or atomic companion remains explicitly
   unresolved and reaches its feature-specific investigation path.  It is not
   partially executed and no empty “success” response is invented.

### Planned validation

- Existing exact 44-byte direct-enter request.
- The same three objects with `27/11` and `7/42` reversed.
- The primary `16/3` with only one documented catalog request.
- The existing teleport-confirm and equipment-swap atomic regressions to prove
  that object-stream routing does not steal those packets.
- `make -j2`, `make boundary-check`, and response framing validation.

### Implementation (2026-07-23)

- Added `vm_net_mock_append_independent_single_object_response` as the single
  implementation of “build a one-object request, recursively route it, validate
  the complete nested response, then append its response objects.”  The generic
  all-independent composer now uses this helper rather than maintaining a
  second copy of the recursive framing logic.
- Replaced the fixed 44-byte / fixed-order direct-enter matcher with
  `mock_scene_runtime_direct_enter_object_stream`.  It first validates the
  `16/3 type=0` runtime-ACK object, then preflights the entire remaining stream.
  It accepts one empty `27/11`, one empty `7/42`, and only explicitly
  independent companions.  It executes the catalog and independent objects in
  request order and merges their valid response objects in that same order.
- Added the runtime-ACK object predicate to the broad teleport-stone detector.
  Therefore a direct-runtime stream containing a future unknown companion is
  left unresolved rather than treating current X as a teleport exit and
  persisting a false destination.
- The client-visible source is now
  `builtin-scene-runtime-direct-enter-object-stream`; its trace reports request
  count, independent companion count, and response count.  It never changes
  scene target or stored role coordinates.

### Validation (2026-07-23)

- `php tmp/unstuck-current-scene-authority-regression.php
  run-direct-runtime-ack 19094`: passed; a standalone runtime ACK remains an
  empty WT response and preserves `01桃花岛_02.sce`.
- `run-direct-runtime-compound-followup 19094`: passed; live 44-byte shape
  returns `27/11 + 7/42` and does not become a `16/3` scene transfer.
- `run-direct-runtime-stream-variants 19094`: passed four variants: original
  order, reversed catalogs, `27/11` only, and `7/42` only.  Each response
  preserved the request object's parser order.
- `php tmp/teleport-stone-cancel-retry-regression.php 19094`: passed, including
  the atomic `16/2 + 16/3` confirmation path and the independent `16/1 +
  2/10` retry frame.
- `php tmp/npc-purchase-equipment-swap-regression.php run 19094`: passed;
  occupied-slot `7/9` replacement remains its dedicated atomic contract.
- `make -j2` and `make boundary-check`: passed.
