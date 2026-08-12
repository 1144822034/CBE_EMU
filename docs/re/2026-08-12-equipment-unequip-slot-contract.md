# Equipment Unequip Slot Contract (2026-08-12)

## Scope

Fix equipment removal through the backpack/equipment screen without changing
client state or accepting an ambiguous selector.

## Confirmed Contract

`JianghuOL.CBE:HandleItemOperationResponse(0x01033544)` consumes response
`1/7/8` with `type=4`, `result=1`, and `seq`.  On success it copies the pending
equipped 324-byte item record into the backpack, replaces the record sequence
with the response `seq`, clears the pending callback, and rebuilds the scene
status meter.

The client request only supplies an equipped-row `seq` in the affected path.
Equipped row identity is not a backpack-instance identity: the existing login
and `1/7/9` replacement contracts define it as `slot + 1`, in the closed range
`1..VM_NET_MOCK_EQUIP_SLOT_COUNT`.

## First Deviation

The server received valid `1/7/8 type=4` requests such as `seq=2`, `3`, `4`,
`5`, and `8`, but interpreted the value as a vague selection.  It only allowed
that path when exactly one equipped item existed, and otherwise returned
`reason=ambiguous-seq`.  This is why removal stopped working once more than one
equipment slot was occupied.

## Repair

`vm_net_mock_role_unequip_item()` now resolves a nonzero `seq` exactly as
`slot = seq - 1`, rejects only out-of-range or empty slots, and validates a
simultaneously supplied item id against that exact slot.  The returned response
still supplies the newly allocated backpack instance sequence, which is the
different sequence namespace consumed by `0x01033544`.

This is a protocol correction, not a fallback: a malformed sequence no longer
causes another equipped item to be selected.
