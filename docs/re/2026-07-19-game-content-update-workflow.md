# Jianghu OL Game Content Update Workflow

## Scope

This record documents the original client update paths and the server/admin
implementation that exposes them. The implementation remains packet-driven;
it does not patch CBE/CBM code or force client globals.

## Static client evidence

Binary selection was made by `binary_name=江湖OL.CBE`.

- `handle_version_update_response(0x01037473)` handles WT kind `18`.
  Subtype `5` reads `result` as a bit mask. Bit 0 starts the module download
  state, while clear bits mark the corresponding module slots ready.
- `send_update_chunk_request(0x01036D80)` has three relevant modes:
  - mode 1 sends WT `18/5`, field `cbm`, as four `(slot, local_version)`
    pairs;
  - mode 2 sends WT `18/6` with `id`, `start`, `version`, and `client`;
  - mode 4 sends WT `18/7` with `name`, `screen`, `type`, `start`, and
    `version`.
- The module table stored in the CBE data segment names the four slots in
  order:
  1. `mmTitleMstarWqvga.cbm`
  2. `mmGameMstarWqvga.cbm`
  3. `mmBattleMstarWqvga.cbm`
  4. `mmShopMstarWqvga.cbm`
- `handle_update_chunk_response(0x010372D6)` reads
  `totalsize/crc/version/data`. Subtype `7` additionally reads `type/name`.
  Its checksum is the cumulative sum of signed payload bytes.
- `WriteResCbmToTempFile(0x01036F48)` installs subtype-6 module bytes through
  `MMORPGTempcbm` and commits the response version to the local module version
  table.
- Named subtype-7 resources are written through
  `WriteResBinToTempFile(name, ...)`.
- `startup_update_net_callback(0x0103B95A)` and
  `startup_handle_update_target_metadata(0x0103B59A)` also parse WT `18/9`.
  A metadata object with `type=0` continues the normal startup path after no
  module update is required.

## Confirmed protocol contract

### Startup module channel

1. Client starts and sends WT `18/9`.
2. Server replies with WT `18/5.result`. Bits 0..3 represent Title, Game,
   Battle, and Shop respectively.
3. For each selected slot, the client sends WT `18/6` chunks. `id` is the
   slot number and `start` is the byte offset.
4. The server returns no more than `0x1000` bytes per response and sets:
   - `totalsize`: full module byte length;
   - `crc`: cumulative signed-byte sum through the returned chunk;
   - `version`: the published module version;
   - `data`: current bytes.
5. After the final chunk, the server records the published version as
   delivered for the hashed pre-login client identity. Failed/incomplete
   transfers therefore remain eligible on the next startup. The admin page
   can reset this ledger if the client cache was manually cleared.
6. When no bit is required, the response also carries WT `18/9` metadata with
   `type=0`, allowing the startup screen to proceed.

The server additionally accepts the client's WT `18/5.cbm` version catalog.
When present, local slot versions are compared directly with the published
versions instead of using the delivery ledger.

### Named resource channel

SCE, MAP, XSE, ACTOR, GIF, and similar resources use WT `18/7`. This path is
on-demand: publishing a named resource does not make the unmodified client ask
for it at startup. To require immediate startup installation, package the
change into the appropriate CBM and publish that module slot.

Dynamic NPC Actor loading has one additional emulator-side safety bridge. The
stock scene parser can discover a missing Actor before its resource-name queue
has been allocated, so entering its normal missing-resource branch writes via
a null base (`30 * 11 == 0x14A`). For safe ASCII `.actor/.gif` names, the host
file-open layer therefore sends a synchronous WT `18/7` request before it
returns “not found” to the CBE. The request carries `clientmiss=1`; the server
serves it only when that exact name is enabled in the named-resource catalog.
The client writes to a same-directory temporary file, validates the cumulative
signed-byte checksum after every chunk, atomically renames it, and retries the
original file open. This is a real CBMS/WT transfer and never reads or copies
the server resource tree directly.

## Server implementation

- Update configuration is persisted as
  `JHOnlineData/server_update_catalog.tsv`.
- Completed client/module versions are persisted as
  `JHOnlineData/server_update_delivery.tsv`.
- With no catalog file, all four module slots default to disabled. This keeps
  existing clients on the normal startup path.
- Module and named payloads prefer the configured `CBE_RESOURCE_ROOT`; the
  development fallbacks use the authoritative `web/fs/JHOnlineData` tree
  before any writable client cache.
- Resource names reject path separators, control bytes, and `..` traversal.
- Managed payloads are bounded to 1 MiB and chunked at `0x1000` bytes.
- Automatic Actor/GIF file-miss requests are additionally restricted to safe
  ASCII leaf names and to catalog entries published by the admin workflow.
- A narrow WT `18/5 + cbm` detector runs before broader fallback handlers.

## Admin workflow

Open the password-protected admin site and select **游戏内容更新管理**.

### Publish a startup module

1. Replace the corresponding file in the displayed server resource root.
2. Set a non-zero version different from the previously published version.
3. Enable **启动时下发** and save.
4. Restart the game client. The server log should show
   `mock_update_version result=0x..`, followed by
   `mock_update_chunk subtype=6`, and finally
   `mock_update_module_complete`.

Changing bytes without changing the published version is intentionally not a
new release. Use **清空记录** only for testing or when client update files were
manually deleted; normal releases should bump the version.

### Publish a named resource

1. Select an existing resource with the searchable dropdown.
2. Set its response version and publish it.
3. Cause the client to enter/load content that references that resource.
4. Confirm a `mock_update_chunk subtype=7` trace for the expected name.

Saving or restoring an enabled dynamic NPC performs steps 1-2 automatically
for the Actor and every GIF referenced by that Actor. No client cache file is
created by the Web process; each client downloads on first use.

## Validation status

- Windows `obj/main.o` compilation succeeds.
- The admin login, update page rendering, slot persistence, named-resource
  publish/remove, and delivery-ledger reset were exercised through HTTP on a
  separate local test port.
- A service-protocol test published Game slot version 7, observed
  `result=0x02`, downloaded the 48,858-byte CBM in 12 chunks, verified every
  cumulative signed-byte checksum, verified response version 7, and observed
  the next startup return `result=0` plus metadata subtype 9.
- A full in-client published-CBM installation still requires an intentional
  test release because enabling a module changes subsequent client startup
  behavior.
- A clean-cache dynamic-NPC test downloaded `e_deity.actor` (456 bytes) and
  `e_deity.gif` (3,696 bytes) through WT `18/7`, verified both SHA-256 hashes
  against the authoritative source, remained stable in-scene for 50 seconds,
  and made no repeat request on the next cache-hit login.
