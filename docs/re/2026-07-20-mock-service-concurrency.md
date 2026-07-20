# Mock service concurrent connection handling (2026-07-20)

## Problem

`vm_net_mock_service_run_forever()` previously accepted one socket and then ran
the complete receive / request-build / MySQL / send cycle on the listener
thread.  A client that sent only part of a frame could therefore block every
other game client (and the HTTP administration listener) indefinitely.

The request builder cannot simply be called concurrently.  It temporarily
restores one account snapshot into a large set of legacy globals and also
mutates shared online-session, nearby-player, social, team, trade, duel, guild
and task state.  The native MySQL client also used one protocol socket, whose
packet stream would be corrupted by concurrent queries.

## Implemented boundary

- The listener now only accepts and enqueues sockets.
- A bounded pool owns per-worker request and response buffers.
- Default pool size is 4, configurable with `CBE_MOCK_SERVICE_WORKERS` and
  clamped to 2..16.
- The accepted-connection queue holds at most 128 sockets.  Excess connections
  are closed with an explicit `queue-full` warning instead of allowing
  unbounded memory/thread growth.
- Accepted sockets use 5-second receive/send timeouts.
- Frame receive and response send run outside the legacy protocol mutex.
- Ping frames are handled without the protocol mutex.
- Account restore, request building, account capture, cross-client state and
  admin mutations remain in one atomic protocol critical section.  This is
  intentional: parallel execution there would cross-wire account globals.
- Each worker now owns a thread-local persistent MySQL socket and error buffer,
  preventing MySQL packet/result interleaving and preparing independent future
  request contexts for finer-grained execution.
- Request logs expose `queue_wait_ms`, `state_wait_ms`, `state_hold_ms` and
  `process_ms` so contention can be measured instead of inferred from client
  pauses.

This is transport-level concurrency plus safe state serialization.  It removes
head-of-line blocking caused by slow socket I/O and lets multiple clients be
accepted/read/replied independently.  True simultaneous legacy request builds
will require replacing the restore/capture globals with an explicit
per-request account context, followed by narrower locks for team/social shared
objects.

## Regression

`tmp/mock-service-concurrency-regression.php` keeps one game connection open
after sending a header that declares an incomplete body, then logs in a second
client.  The second login must complete in less than 1.5 seconds.  It also
submits two account logins before reading either response and verifies both
responses.

Observed on the final build:

```text
mock service concurrency regression passed slow_peer_login_ms=235 login_a=141 login_b=141
```

Additional checks:

```text
game type scope regression passed bootstrap=73 scene=11 combo=17
admin_status=200 admin_bytes=3942
make -j2: passed
```

