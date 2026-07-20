# 2026-07-20 Backpack Grid Reseed on Role Login

## Symptom

After a character entered the scene, opening the backpack showed no items even
though the role still had persisted inventory rows.

Runtime evidence for role `10023` separated storage from delivery:

- the scene startup response logged `mock_backpack_items ... rows=5`, proving
  the MySQL role backpack was loaded and `1/17/1` could be serialized;
- the real client login's first `5/10 + 7/7(type=1)` response was only 186
  bytes and had no `mock_backpack_grid` trace;
- an earlier connection for the same account had already emitted `1/30/21`,
  and opening the shop manually cleared the guard, after which the next group
  response emitted the missing five-row `1/30/21` object.

## Client Contract

- `JianghuOL.CBE:0x01039952` handles `1/30/21`, reads `result`, `gridnum` and
  the raw item rows, and inserts them into the main item manager.
- `mmGameMstarWqvga.cbm:0x418C` handles `1/17/1` while the backpack component
  is active. It is a full visible-list refresh and does not replace the login
  bootstrap contract for the newly created main item manager.
- `mmGameMstarWqvga.cbm:0x2434` opens the backpack component and requests
  `1/7/42`; the mock's backpack-open response pairs that with `1/17/1`.

## Root Cause

`g_netMockBackpackGridSeededRoleId` is captured in account state. A previous
connection or regression client could therefore leave the same role marked as
already seeded. A later successful role select creates a fresh client item
manager, but the account-scoped marker incorrectly suppressed its one required
`1/30/21` bootstrap.

## Fix

Successful `1/1/6` role selection now clears the backpack grid duplicate guard
and stale shop-list one-shot state. The immediately following type-1 group
request can then emit exactly one fresh `1/30/21` object for the selected role.
The server logs `mock_backpack_grid_reseed ... reason=title-role-select`, followed
by `mock_backpack_grid ... kind=30 subtype=21` when the grid is delivered.

Do not append `1/30/21` to every response. Repeated grid insertion duplicates
stack rows in the client; the existing one-shot guard remains active after the
first group response in the new role lifecycle.

## Validation

- `make -j2` completed and relinked `bin/main.exe`.
- The service regression selected role `10023`, sent the real 35-byte
  `5/10 + 7/7(type=1)` request twice, selected the same role again, and repeated
  the group request:
  - first lifecycle: response `363`, contains five-row `1/30/21`;
  - duplicate request: response `186`, contains no `1/30/21` replay;
  - second role lifecycle: response `363`, contains a fresh five-row `1/30/21`.
- An empty `1/7/42` backpack-open request returned a 156-byte
  `1/17/1 + 1/7/42` response, and the `1/17/1` trace reported the same five
  persisted rows.
- Service stderr remained empty after the regression.
