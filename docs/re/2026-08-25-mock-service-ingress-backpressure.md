# Mock service ingress backpressure (2026-08-25)

## Trigger and evidence

The remote hangup stall coincided with service lines such as:

```text
dropped malformed request worker=3 sequence=892941 queue_wait_ms=4981
```

The remote client and service both use a 5000 ms socket timeout. The previous
service accepted a socket and immediately put it into the four-worker protocol
pool; a worker did not read the `CBMS` header until after its queue wait. A
partial, abandoned, or delayed public connection could therefore occupy a
worker for the complete receive timeout, and normal battle/scene requests
could wait until their client peer had already given up.

The old `dropped malformed request` message was also ambiguous: the same path
covered an incomplete `CBMS` frame and a response send to a client that had
already timed out. It was not proof that the game emitted an invalid WT
packet.

## Change

Game sockets now enter a bounded listener-side ingress table before the
protocol worker pool:

1. The listener watches at most 32 accepted game sockets.
2. Only a complete, version-1 `CBMS` frame whose declared body fits the normal
   request buffer is handed to a protocol worker.
3. An incomplete frame is retained for up to 2000 ms, then closed and logged
   as `ingress_drop reason=frame-timeout`. A closed peer or invalid header is
   also logged at the ingress boundary.
4. Once a frame is complete, the existing worker reads, parses and responds to
   the original bytes unchanged. Its existing serialized protocol/MySQL
   critical section is not relaxed in this change.

The canonical transport ping (`CBMS`, version 1, flags `PING`, zero body and
zero metadata) is an explicit exception.  The former worker handler returned
an empty `CBMR` before acquiring protocol state; the listener now returns that
identical 20-byte frame as `ingress_ping`.  Non-canonical ping flags continue
through the existing handler.

Request timing now distinguishes `ingress_wait_ms` (TCP accept to complete
frame) from `queue_wait_ms` (complete frame to worker start). A continuing
large `state_wait_ms` or `state_hold_ms` is evidence for the next, separate
task: replacing the legacy global account restore/capture state with explicit
per-request contexts before narrowing the protocol lock. The service emits
these timing fields automatically for a wait above 100 ms or a state hold /
request process above 250 ms; full verbose logging is not required.

## Admin isolation

The HTTP administration listener now owns a separate two-worker pool
(`CBE_MOCK_ADMIN_WORKERS`, clamped to 1..4).  The game listener keeps the
existing `CBE_MOCK_SERVICE_WORKERS` pool (default 4).  This prevents an
incomplete or slow HTTP request from consuming game transport workers.  Admin
handlers still acquire `g_vm_mock_service_protocol_mutex` for shared account
and persistence state, so this is an I/O isolation boundary rather than an
unsafe parallel-state change.

## Contract boundary

No CBE memory, register, callback or response bytes are changed. This is a
server TCP admission change only: valid WT payloads still traverse
`vm_net_mock_service_handle_client()`, `vm_net_mock_process_request_bytes()`
and the normal `CBMR` response path.
