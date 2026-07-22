# Backpack Equipment Operation

Status: implemented for empty-slot equip/unequip from `WT 1/7/8`, and for an
occupied-slot replacement from the distinct `WT 1/7/9 + 1/2/10` transaction.

## Symptom

- Runtime after clicking a backpack weapon and choosing equip:
  - request: `wt=7/8 len=44`
  - old response source: `builtin-game-type`
  - old response length: `36`
- The generic game-type fallback sees `type=3` and rewrites the response
  subtype to `7/32`.  The item-operation screen is waiting for subtype `7/8`,
  so the loading bar keeps spinning.

## Client Parser Evidence

- `JianghuOL.CBE:0x01033544 HandleItemOperationResponse`
  - dispatches on response object subtype.
  - subtype `8` reads field `type`.
  - `type == 3` is the equip branch:
    - reads `result`;
    - if `result == 1`, copies the pending backpack item from `R9+38024`;
    - calls the equipment-manager operation at item manager vtable `+104`;
    - reads field `seq` and writes it into the equipped item at `+276`;
    - calls the pending UI callback at `R9+38028`;
    - rebuilds the status meter and clears the wait flag.
  - `type == 4` is the add-to-backpack branch used by unequip/acquire:
    - reads `result`;
    - if `result == 1`, copies the pending item from `R9+38020`;
    - calls item-manager vtable `+100`;
    - reads field `seq` and writes it into item `+276`;
    - calls the pending UI callback at `R9+38028`;
    - rebuilds the status meter and clears the wait flag.

## Request Contract

- Single object:
  - `1/7/8`
  - required semantic field: `type = 3` for equip, `type = 4` for unequip
  - item selector fields accepted by the mock:
    - `seq`, `itemseq`, `itemSeq`
    - `itemId`, `itemID`, `itemid`, `id`

The detector is intentionally narrow.  It only consumes `WT 7/8` item-operation
requests with documented `type` values `3` or `4`.

## Occupied-slot replacement contract

The wooden broad sword repro does not follow the `7/8 type=3` path: the
starter weapon already occupies its weapon slot.  Runtime recorded two valid
encodings of the same exchange:

- standalone `WT 7/9 len=30`: `1/7/9`, payload length 21;
- compound `WT 7/9 len=45`: the same `1/7/9` followed by `1/2/10`, payload
  length 10.

The `1/7/9` payload is `body:u16`, `bag:u16`.  `1/2/10` is an optional actor
transport companion, not an equipment field and not a requirement for the
completion response.

`JianghuOL.CBE:0x010328D4 BuildGameEventPacket` writes `body` and `bag` when
the event subtype is 9.  The calling UI selects the current equipment row for
`body` and the selected backpack row for `bag`.  The equipment catalog sent by
the mock encodes equipment row sequence as `slot + 1`, so the server verifies
that relation before changing state.

`JianghuOL.CBE:0x01033544 HandleItemOperationResponse` case 9 parses **only**
`result`.  On `result=1` it locally replaces the current equipment with the
selected backpack item, returns the old item to the backpack using the
selected backpack row sequence, runs the pending callback, and clears the
wait layer.  Therefore the response must be precisely:

- `1/7/9 { result:u8 }`.

The server mirrors the same atomic sequence-preserving replacement before it
sends success.  It does not model this as consume-plus-append, because append
would assign the old item a new backpack sequence and diverge from the client.
When present, the `1/2/10` companion is checked as an exact optional trailing
object but needs no response object for the item-operation completion.

Expected trace:

```text
mock_item_equip_swap body=<slot+1> bag=<backpack-seq> companion_2_10=<0|1> item=<new-id> old=<old-id> slot=<slot> result=1 reason=ok resp=7/9
```

## Response Contract

- Single object:
  - `1/7/8`
  - fields:
    - `type = <request type>`
    - `result = 1` on success, `0` on server-side failure
    - `seq = <equipped item seq>` for equip, or the new backpack seq for unequip

Dispatch source: `builtin-item-equip`.

## Server State

On success the mock server:

Equip (`type=3`):

- looks up the selected backpack item on the active role;
- verifies it exists in `equip.dsh`;
- checks the role level against the equipment requirement;
- consumes one item from the backpack stack;
- writes the item id to `role->equippedItemIds[slot]`;
- returns the previously equipped item in that slot to the backpack when there
  is capacity or an existing stack;
- syncs derived HP/MP/stat caps;
- saves the role DB with reason `item-equip`.

Unequip (`type=4`):

- resolves the active role equipment slot from `itemId` when present;
- if only `seq` is present, accepts it only when exactly one equipment slot is
  occupied, otherwise fails as ambiguous;
- adds the item back to the active role backpack and returns the new backpack
  `seq`;
- clears the equipment slot;
- syncs derived HP/MP/stat caps;
- saves the role DB with reason `item-unequip`.

Expected trace:

```text
mock_item_equip type=3 requested_item=<id> requested_seq=<seq> item=<id> seq=<seq> slot=<slot> old=<id> result=1 reason=ok resp=7/8
mock_item_equip type=4 requested_item=<id> requested_seq=<seq> item=<id> seq=<new-seq> slot=<slot> old=0 result=1 reason=ok resp=7/8
```

## Negative Evidence

- Do not let this request reach `builtin-game-type`.  `type=3` there is a
  generic game status query and produces `7/32`, not an item-operation
  completion packet.
- Do not patch client pending-item globals.  The client already performs the
  local UI transition when it parses `7/8 type=3 result seq`.
