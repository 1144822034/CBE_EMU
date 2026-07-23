# Jianghu OL client/service process boundary

## Goal

The emulator client and the Jianghu OL service must be independently
executable processes.  A service request handler must not inspect CBE guest
memory, and the CBE client must not load MySQL state, server resource catalogs,
HTTP administration routes, or response builders.

The phrase "no client/server data reads" does **not** prohibit network
communication.  The explicit, allowed contract is a copied CBMS frame over a
TCP socket:

```text
CBE guest request bytes
  -> client-only CBMS adapter -> TCP -> service WT parser/builders
  -> TCP -> client-only adapter -> CBE guest response bytes
```

Thus the server receives only the request fields the client intentionally
transmits, and the client receives only the response bytes the server
intentionally transmits.  Neither side maps the other's address space or
opens the other's persistence/resource paths.

## Previous deviation and evidence

The Windows `Makefile` produced only `bin/main.exe` without either
`CBE_CLIENT_ONLY` or `CBE_SERVER_ONLY`.  It therefore included the desktop CBE
emulator and `src/server/mock-server.c` in the same process.  Server builders
had legacy optional fallbacks that read `Global_R9` scene nodes and battle CBM
tables.  That violates session isolation: `Global_R9` belongs to one local CBE
instance, not to the TCP client whose frame is being handled.

The relevant first-deviation sites were:

- runtime scene name: `mock_server_catalog.c` reading `Global_R9 + 0x54AC`;
- local pending scene-change test: `mock_server_scene_task.c` reading guest
  transition fields;
- battle wire/template selection: `mock_server_battle.c` reading the local
  battle CBM table;
- login callback repair and local response buffers: core/transport code
  writing CBE callback or response pointers.

Those reads could make a remote user's server response depend on an unrelated
locally launched emulator.  They were also the same class of ambiguity found
in the earlier host-global-scene authority audit
([2026-07-23-host-global-scene-authority-audit.md](2026-07-23-host-global-scene-authority-audit.md)).

## Corrected contract

### Build products

On Windows, `make` now builds two targets with separate object directories:

| Target | Compile mode | Owns |
| --- | --- | --- |
| `bin/main.exe` | `CBE_CLIENT_ONLY` | CBE VM, UI, resource installation, client socket adapter |
| `bin/jh-online-server.exe` | `CBE_SERVER_ONLY` | CBMS listener, WT handlers/builders, MySQL, service resource catalogs and admin HTTP listener |

The client is linked without `mysql-client.c` or any mock-service fragment.
The service is linked without SDL or Unicorn runtime libraries.  Separate
`obj/client` and `obj/server` directories prevent an object compiled for one
process role from being reused for the other.

The client cache is `JHOnlineData/`.  In `CBE_CLIENT_ONLY` builds, it no
longer falls back to `web/fs/JHOnlineData/` or `../web/fs/JHOnlineData/` when
opening a bare `.dsh` resource.  Those locations are deployment paths owned
by the service.  A cache miss instead reaches the existing `WT18/7`
`vm_file_try_download_named_resource` flow, which validates the response and
installs the received bytes into the client cache.

### Server authority changes

`CBE_SERVER_ONLY` now does the following at the ownership layer rather than
using host-memory fallbacks:

- runtime scene lookup returns no local scene; callers use the active service
  session and durable role scene instead;
- local pending-transition matching is disabled; session transition state is
  authoritative;
- the battle collision id normalized from the WT request / server monster
  catalog is used directly.  The remote client must obtain any missing battle
  resource via the normal resource-update protocol;
- login callback repair is client-side only;
- in-process `on_send`, response-buffer copying and `read_data` bridge entry
  points cannot access CBE pointers in the service build.

An initial compatibility sentinel (`src/server-headless.c`) was also removed
from the service target after a no-sentinel link and standalone CBMS regression
passed.  This means the server artifact now contains neither a guest-memory
stub nor `g_cbeInfo`; it does not create an emulator, load a CBE, or expose any
client memory.

## Running the two processes

Start the service from the project root (or pass a resource root explicitly):

```powershell
bin\jh-online-server.exe `
  --resource-root=E:\DevOs\CBE_EMU\web\fs\JHOnlineData `
  --mock-service-bind=127.0.0.1 `
  --mock-service-port=19090
```

Then run `bin\main.exe` with the client endpoint set to the service, for
example `CBE_SERVER_ENDPOINT=127.0.0.1:19090`.  `main.exe` no longer provides
`--mock-service-only`; that would reintroduce the combined-process boundary.

## Verification

`make boundary-check` verifies both outputs after a full build:

- client has no service listener, MySQL, administration, or response-builder
  symbols;
- service has no CBE client transport bridge symbols and no SDL/Unicorn runtime
  imports;
- service has no `uc_mem_*`, `uc_reg_*`, `Global_R9`, or `g_cbeInfo` symbols;
- client contains no direct `web/fs/JHOnlineData` deployment-path fallback.

Before removing the compatibility sentinel, a probe linked from the exact
server objects **without** `server-headless.o` and was started on port 19093.
It passed `tmp/mock-service-concurrency-regression.php 19093`
(`slow_peer_login_ms=3`, two parallel login responses of 141 bytes), proving
the serving path never required the sentinel.

The standalone service was started with
`bin/jh-online-server.exe --mock-service-port=19093 --mock-admin-port=18093`.
It returned a CBMR ping response and passed
`tmp/mock-service-concurrency-regression.php 19093`
(`slow_peer_login_ms=3`, two parallel login responses of 141 bytes).  The
older `login-scene-ready` assertion that expects a welcome message in one
specific response was already failing against the pre-separation service on
port 19090; it is not treated as proof of this boundary change.

## Remaining risk

The service fragments intentionally remain one aggregation translation unit
because their historical static protocol state still depends on source order.
They are process-separated now, but future work should continue moving that
state behind explicit request/session contexts.  Any new server code that
needs client state must add a WT request field or a session-owned state field;
it must not add a `Global_R9`, `MTK`, CBE screen, CBE node fallback, or a
client-side direct service-resource path.

`src/main.c` is still a historical monolith: preprocessing a server build
retains unreachable emulator helper definitions so that the shared static
declarations keep their legacy source order.  `-fwhole-program` and section
GC remove them from `jh-online-server.exe`; the enhanced artifact gate confirms
that neither the raw Unicorn calls nor their VM/DF/CBFS/cache wrappers are
present in the service binary.  This is therefore not a runtime data-boundary
violation, but it is a source-level maintenance risk.  A future structural
phase should move the CBE entry/runtime into a client-only translation unit and
leave the server with an explicit service-entry module.
