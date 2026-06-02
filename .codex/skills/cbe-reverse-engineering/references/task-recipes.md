# Task Recipes

## Firmware triage

1. Identify container boundaries, load addresses, and obvious asset/code regions.
2. Search strings for domains, ports, game names, update filenames, or VM markers.
3. Record durable layout findings in `docs/re/firmware-map.md`.

## Protocol recovery

1. Start from existing notes in `docs/net_mock_protocol.md`.
2. Find request builders and response parsers in code or decompilation.
3. Document framing, object layout, field encodings, and state transitions in `docs/re/protocol.md`.
4. Only then extend mock-server or backend code.

## Emulator/runtime work

1. Identify the platform contract the client expects.
2. Implement the narrowest behavior that matches the contract.
3. Avoid direct game-state forcing when a platform semantic fix is possible.
4. Record the confirmed contract in `docs/re/runtime-contracts.md`.

## Server work

1. Mirror the protocol document rather than inventing packet shapes in code.
2. Keep handlers minimal and deterministic.
3. Prefer fixtures from `samples/packets/` when adding tests.
