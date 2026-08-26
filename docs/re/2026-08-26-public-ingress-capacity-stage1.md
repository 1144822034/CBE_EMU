# Public ingress capacity: stage 1 admission protection

Date: 2026-08-26

Status: implemented; runtime validation awaiting isolated MySQL credentials

## 1. Current bottleneck and small-scope target

The public game listener repeatedly emitted `connection_rejected kind=game
reason=ingress-full`.  The supplied terminal captures show both incomplete
connections that expired after 2000 ms and completed header-only `CBMS` frames
from unrelated public IPv4 sources.  This is an admission problem before any
WT packet is parsed; it is not evidence of a wrong CBE response contract.

The small target for this stage is to keep header-only transport probes and a
single slow peer from consuming the game-worker queue or all 32 incomplete
frame slots.  It intentionally does not alter WT bytes, game-state ownership,
client polling cadence, or the client callback/event path.

## 2. Runtime evidence

- User capture: repeated `connection_rejected kind=game reason=ingress-full`.
- User capture: `ingress_drop ... reason=frame-timeout waited_ms=2000` from
  public peers that did not provide a complete frame.
- User capture: repeated `ingress_frame_ready ... bytes=24 source=<public-ip>`.
  The deployed capture predates the current 20-byte transport-header build,
  but in both variants the record means a complete transport header reached
  ingress, not a gameplay WT request.
- Existing isolated regression `mock-service-ingress-backpressure-v1` proves
  a canonical `CBMS` ping must receive an empty `CBMR` without being delayed
  by incomplete game or admin sockets.

## 3. Client / transport contract evidence

The IDE/IDA connector is not available in this session, so no new disassembly
claim is made.  The checked-in client transport source and prior runtime
record are sufficient for this transport-only change:

1. `src/network-client.c:vm_net_mock_remote_scene_sync_poll()` sends a normal
   `CBMS` frame and consumes a normal `CBMR` response on its socket.
2. `src/server/mock_server_transport.c:vm_net_mock_service_handle_client()`
   replies with an empty `CBMR` immediately whenever the `PING` bit is set,
   before request-length validation or acquisition of the legacy protocol
   mutex.
3. The ingress fast path already returns the same empty `CBMR` for the exact
   canonical ping form.  Extending that boundary to every frame with the
   `PING` bit preserves the existing handler result; it merely avoids a worker
   round trip for bytes the handler ignores.
No CBE/CBM instruction, VM memory, register, parser, callback, or WT response
will be changed.

## 4. Capacity context and excluded hypotheses

- A 32-entry ingress table is a bound for *incomplete TCP frames*, not an
  online-player cap.
- The 128-job worker queue and up to 16 workers do not make gameplay state
  parallel: valid game requests still hold the legacy global protocol mutex.
- The client-side scene poll remains at its existing one 100-ms scheduler
  tick.  This stage is deliberately service-only.
- A global 100-player claim remains unresolved until the legacy global
  restore/capture context is replaced or benchmarked under realistic movement
  and database load.

## 5. Planned narrow implementation

1. Admit at most four simultaneously incomplete frames from one source IP;
   normal frames leave ingress as soon as their complete header/body is
   available, so this is not an online-player or per-account quota.
2. Answer every `PING`-flagged frame directly at ingress with the existing
   empty `CBMR` contract, exactly as the worker handler already does.
3. Extend the isolated ingress regression with a fifth incomplete loopback
   peer and a non-canonical ping-bit frame.

## 6. Validation criteria

- Four incomplete peers from one source are still reclaimed at the ingress
  timeout; the fifth is rejected without entering the worker queue.
- Canonical and non-canonical `PING` frames both receive a 20-byte empty
  `CBMR` within 1.5 seconds.
- A valid login still completes while these peers exist.
- No client source or scene-poll cadence changes are included.

## 7. Implementation and validation result

- Implemented in `src/server/mock_server_transport.c`:
  - a source may retain at most four *incomplete* first frames at ingress;
    the fifth is reclaimed as `source-pending-cap` while a complete frame from
    the same NAT address is still dispatched normally;
  - all `PING`-bit frames receive the same zero-body `CBMR` at ingress that
    `vm_net_mock_service_handle_client()` previously returned before state
    locking.
- `make -j2` passed for both `bin/main.exe` and `bin/jh-online-server.exe`.
- `git diff --check` and PowerShell parsing for
  `scripts/run-mock-service-ingress-regression.ps1` passed.
- The isolated ingress scenario was not run: this shell has neither
  `CBE_AUTOMATION_MYSQL_PASSWORD` nor `CBE_AUTOMATION_PHP`.  It will create
  its own loopback service, disposable database, and artifact directory once
  those test-only prerequisites are supplied; it will not touch the running
  public service.

## 8. Stage 2: success-log and flush pressure

The supplied capture shows normal `ingress_frame_ready` records with roughly
37--65 ms of ingress wait.  The current implementation emits such a record
for every complete non-ping frame once accept-to-ready time exceeds 20 ms.
That is expected on a WAN path, rather than evidence of server congestion.
The PING ingress fast path has the same condition.  Thus routine traffic can
format one or more success records per 100-ms scene-poll cycle.

The scene-poll handler also calls `fflush(stdout)` on every completed poll,
even when it did not print a timing diagnostic or a response body.  At 100
players and the unchanged 100-ms client cadence this reaches roughly 1000
flush attempts per second.  It is outside the protocol mutex, but still adds
stdout lock and sink I/O work to every service worker.  This contradicts the
repository logging policy's requirement that INFO not be emitted for every
packet.

Implemented service-only change:

1. Leave error and warning records intact, but send successful ingress-ping
   and frame-ready records only through the existing
   `CBE_MOCK_VERBOSE_LOG=1` diagnostic mode.
2. Flush stdout only immediately after an actual slow-request or verbose
   diagnostic record has been emitted.  Packet bytes, event types, locking,
   queueing, and client polling remain unchanged.
3. Correct the isolated regression's environment variable from the obsolete
   `CBE_MOCK_VERBOSE` spelling to `CBE_MOCK_VERBOSE_LOG`, so its ingress-path
   evidence remains available without enabling production packet logs.

This is a code-derived hotspot, not yet a CPU benchmark.  Its expected benefit
is removal of routine stdout work; it does not remove the serialized legacy
state domain.  The original ingress regression continues to enable the
existing verbose mode explicitly, so it retains the ping-path log assertions
without turning on production packet-success logging.

Validation: `make -j2`, `git diff --check`, and PowerShell parsing of
`scripts/run-mock-service-ingress-regression.ps1` passed.  The full isolated
scenario remains pending because this shell does not have the test-only
`CBE_AUTOMATION_MYSQL_PASSWORD`; it will only use its disposable loopback
service and database when that prerequisite is present.

## 9. Remaining capacity boundary

The stage removes public admission pressure.  It does **not** make game-state
handling concurrent: movement, combat, database-backed actions, and scene
poll construction still serialize on the legacy protocol mutex.  A real
100-player active-motion claim therefore requires an isolated multi-client
load scenario plus a later explicit per-account request context / narrower
state-lock change.  Increasing worker count, ingress slots, or the connection
queue alone would not provide that property.
- `make -j2` passes.  No user service, port, account, or database is touched;
  the scenario starts its own loopback service and disposable database.
