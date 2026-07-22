# Equipment swap operation `WT 7/9` (2026-07-22)

Status: investigating the request selectors; client response contract confirmed.

## Reproduction

1. Open the backpack.
2. Select the equipment `1001` (wooden broadsword / 木制宽剑).
3. Choose equip.

The server received and then asserted on this previously unhandled packet:

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

## Current unknowns and first divergence

The first object has a 21-byte payload.  Its exact selectors still need to be
captured before changing persisted state: the source may carry the selected
backpack item id and sequence, and the second `1/2/10` object may carry an
operation context rather than an independent game request.

The first invalid state is clear: `vm_net_mock_build_response` has no narrow
handler for `1/7/9` plus its companion `1/2/10`, so it reaches the global
unhandled assertion before a response is built.

## Next probe and planned repair

Add a temporary bounded field/hex trace only for unhandled packets, reproduce
the 45-byte request once, and record its selectors.  Then add a narrow
`vm_net_mock_is_item_equip_swap_request()` detector and builder that:

1. validates the two-object request grammar;
2. resolves the selected backpack equipment and target slot from those fields;
3. atomically returns the old slot item to the role backpack, equips the new
   item, syncs derived vitals, and persists the role;
4. returns `1/7/9 {result}` so the client completes its own local UI swap.

The older `1/7/8` path remains separate until its currently documented request
grammar is observed again.

