# Ingress partial-frame busy spin

## Trigger and evidence

On 2026-08-28, a Linux `perf record -p 1393161 -g sleep 30` capture of
`jh-online-server` contained 119,552 CPU-clock samples (approximately 29.9
aggregate CPU seconds). The listener thread, TID 1393161, accounted for
79.3% of samples. Matching the recorded libc build ID resolved its dominant
logical paths as:

| Path | Samples | Share |
| --- | ---: | ---: |
| `recv` | 35,339 | 29.56% |
| `ioctl` | 29,816 | 24.94% |
| `select` | 20,782 | 17.38% |

The original listener called `recv(..., MSG_PEEK)` on a readable ingress
socket, then `ioctl(FIONREAD)` to see whether the whole CBMS frame had
arrived. For a complete header followed by a partial body, neither operation
removed bytes from the kernel receive queue. `select()` therefore immediately
reported the same socket as readable and the listener repeated that sequence
until the missing body bytes arrived or the two-second ingress timeout fired.

## Contract and fix

Ingress still owns incomplete frames; workers still receive only complete
frames and continue to execute the existing protocol, persistence, and
response handler exactly once. The listener now owns a dynamically sized
per-ingress frame buffer:

1. Each readable callback consumes available header or payload bytes once.
2. After a validated header, the buffer grows only to that frame's declared
   bounded size.
3. A partial frame remains in ingress and its socket becomes non-readable
   after the available bytes are drained, allowing `select()` to sleep for
   new network input.
4. A completed frame transfers its byte buffer and socket ownership to the
   FIFO worker job. The worker validates and processes the exact buffered
   CBMS bytes through the pre-existing handler; PING retains its listener
   owned empty-CBMR behavior.

No CBE/CBM data, client memory, callback, response bytes, or protocol state
is changed by this host-side transport fix.

## Regression

`make ingress-partial-frame-regression` builds an isolated loopback test. It
sends a PING header in two writes, then a regular frame with a complete header
and deliberately incomplete body. The test asserts that each partial segment
is consumed before the next `select()` wait, verifies the PING CBMR contract,
and invokes the worker's prebuffer boundary. It starts no listener, database,
or client and writes no account state.

The source test is `scripts/ingress-partial-frame-regression.c`. Re-profile
the deployed Linux service after this change to verify that the listener no
longer dominates `recv`/`ioctl`/`select` CPU samples; the original capture did
not identify the source IP that supplied the incomplete frames.
