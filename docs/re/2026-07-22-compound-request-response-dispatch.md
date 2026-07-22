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
- The old splitter recursively constructs one-object requests, but first
  rejects every object not in a static safe list and accepts a subresponse
  without checking its declared WT length or declared response-object count.
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
