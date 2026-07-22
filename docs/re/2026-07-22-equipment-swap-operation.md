# Equipment swap operation `WT 7/9` (2026-07-22)

Status: implemented and regression-tested against the mock-service TCP protocol.

## Reproduction

1. Equip a weapon, then keep a second weapon in the backpack.
2. Select the backpack weapon and choose the replacement action for the
   occupied slot.

Before the dedicated handler was added, the server received and asserted on
this request:

```text
unhandled wt=7/9 len=45 objects=1 first=1/7/9:21,1/2/10:10
```

The unanswered request leaves the client operation wait layer active.  The
problem is therefore an unsupported protocol phase, not a backpack UI timer.

## Confirmed client response contract

IDA selected by binary name `江湖OL.CBE`:

- `HandleItemOperationResponse` (`0x01033544`) dispatches by response subtype.
- For subtype `9`, it reads only the scalar field `result`.
- When `result != 0`, it copies the currently equipped pending item back into
  the item manager, equips the pending backpack item, preserves the two local
  sequence values, invokes the stored UI callback, rebuilds the status meter,
  and clears the wait state.
- `SendEquipUseReq` (`0x01032B8A`) fills the pending equip data then emits the
  operation through the item-manager network vtable. `SendEquipRemoveReq`
  (`0x01032BDE`) follows the same route for count zero.

The response must therefore be a normal event-7 packet containing:

```text
WT 1/7/9 { result: u8 }
```

It must not be redirected to the generic `7/7` game-type handlers and must not
be answered as the older `7/8 {type,result,seq}` operation contract.

## Confirmed request grammar and implementation

IDA selected the `江湖OL.CBE` instance by `binary_name` and shows that
`BuildGameEventPacket(0x010328D4)` builds the 21-byte object payload as:

```text
WT 1/7/9 { body: u16, bag: u16 }
```

- `body` is the equipped-row sequence (`slot + 1` in the persisted equipment
  list).
- `bag` is the selected backpack-row sequence.
- A same-packet `1/2/10` object is an optional scene follow-up.  It is not an
  additional selector of the replacement operation.

The dedicated handler now runs before generic equipment processing.  It
validates both sequences, the target item's equipment slot and level
requirement, then replaces the selected backpack row in place with the old
equipped item.  Keeping the backpack sequence is required because the client
does that same local replacement after receiving success.  The new item is
written to the equipment slot, derived attributes are recalculated, and the
whole role state is saved.  If the relational save fails, the in-memory role
snapshot is restored and the normal `result=0` response is returned; no client
state is force-written.

`7/8` remains a separate contract for equipping into an empty slot.

## Regression

`tmp/npc-purchase-equipment-swap-regression.php` creates an isolated role with
`1001` equipped and backpack row `7001=1002`, sends the client-shaped
`1/7/9 {body=1,bag=7001}` request through a real mock-service TCP connection,
and verifies both `1/7/9 {result=1}` and MySQL state:

```text
equipment slot 0 = 1002
backpack sequence 7001 = 1001, count 1
```
