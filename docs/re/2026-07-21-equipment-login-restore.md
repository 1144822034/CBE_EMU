# Equipment Login Restore

Status: implemented and locally packet-validated.

## Symptom

Equipping an item correctly removed it from the backpack and persisted the
equipment slot in MySQL, but a later role login rebuilt only the backpack item
manager.  The character equipment UI therefore remained empty even though
`account_role_equipment` still contained the equipped item.

This is a missing login response flow, not a client defect.  No CBE/CBM patch
or client-global write is required.

## Client Evidence

`mmGameMstarWqvga.cbm:sub_D04(0x00000D04)` is the response parser for WT
`7/7`:

- reads object field `type` and raw field `iteminfo`;
- reads the raw stream as `rowCount`, then repeated
  `seq(u16), itemId(u32), currentCount(u32), commonEquipmentAttributes`;
- loads the corresponding row from local `item.dsh` or `equip.dsh`;
- for `type=1`, calls item-manager vtable `+52` directly;
- for `type=2`, calls item-manager vtable `+104` with the parsed item and `-1`.

The `type=2` call arguments are confirmed by disassembly at
`mmGameMstarWqvga.cbm:0x00001062..0x00001072`: manager, the 324-byte parsed
item, and `-1`.

`JianghuOL.CBE:SendEquipUseReq(0x01032B8A)` is the item-manager `+104`
operation.  It:

- copies the full item;
- skips removal of a pending backpack row when the third argument is `-1`;
- preserves the item's original DSH category in byte `+283`;
- changes byte `+282` to category `15`;
- inserts the copy through item-manager vtable `+52`.

This is the completed client's native equipment-list bootstrap path.

For equipment ids (`>=1000`), `sub_D04` writes the row's `currentCount` to
item offset `+272`.  The server therefore sends the persisted current
durability in that field.

## Request-Flow Distinction

The role-login runtime chain contains separate WT `7/7 {type=2}` and
`7/7 {type=3}` requests after the initial group request.  Those are existing
status queries whose established responses remain:

- request `7/7 type=2` -> response `7/20 {result,pcimg}`;
- request `7/7 type=3` -> response `7/32 {result,expcard}`.

They must not be repurposed as equipment responses.

The equipment bootstrap is instead appended to the initial combined
`5/10 + 7/7 type=1` login response, after the `30/21` backpack grid object:

```text
1/7/7
  type = 2
  iteminfo = rowCount,
             [seq, itemId, currentDurability, commonEquipmentAttributes]...
```

This response is emitted by the same per-role one-shot lifecycle as `30/21`,
so a repeated group request cannot replay the equipment animation/list insert.
The one-shot is re-armed by a successful role selection, including returning
to the title and selecting the role again.  An empty equipment set still emits
a valid zero-row `iteminfo` stream.

Equipment bootstrap sequence ids are deterministic `slotIndex + 1`.  They are
nonzero, stable across relogin, and identify one of the eight equipment slots.

## Server Trace

Expected successful login trace:

```text
mock_backpack_grid role=<role> kind=30 subtype=21 ...
mock_equipment_login role=<role> rows=<n> iteminfo_len=<len> response=7/7-type2 ...
net_send ... wt=5/10 ... source=builtin-group-type1
```

## Validation

Local service validation used role `10023` with persisted equipment item
`1001`:

- first role lifecycle returned `30/21` followed by one `7/7 type=2` equipment
  row;
- decoded equipment row had a slot sequence in `1..8`, item id `1001`, and
  persisted durability;
- a duplicate group request returned neither `30/21` nor `7/7 type=2`;
- selecting the role again re-armed and returned both bootstrap objects;
- the independent status-query regression still returned `7/20` and `7/32`;
- a full local emulator login for `guest00019` received the expanded group
  response (`282 -> 339` bytes), then the unchanged `34`-byte `7/20` and
  `36`-byte `7/32` responses, reached the scene, and exited without an address,
  unpack, or assertion failure;
- `make -j2` completed successfully.
