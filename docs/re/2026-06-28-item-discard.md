# 2026-06-28 Item Discard

## Problem

Discarding a backpack item crashed because the mock server did not handle the
client request:

```text
WT 7/4 len=38 objects=1 first=1/7/4:29
```

## Request Evidence

- Runtime signature: one WT object, `major=1 kind=7 subtype=4`, payload length
  `29`.
- The request payload is parsed narrowly as an item selector:
  - `seq`, `itemseq`, or `itemSeq`
  - optional `id`, `itemId`, `itemID`, or `itemid`
  - optional `count` or `num`
- Lookup prefers `seq` when present, because older CBE request builders may
  reuse `id` for a non-item identifier in mixed UI flows.
- `count`/`num` is not required by the request shape. The retained production
  packet signature does not include a raw field dump, but the reported
  reproduction is decisive for the no-count path: a stack of ten mooncakes
  loses all ten after one UI discard. The server's former no-count branch was
  the only point in the discard chain that could turn that selector-only action
  into a whole-stack removal.
- The fixture deliberately uses the accepted selector-only form
  `type:<u8> + seq:<u32> + id:<u32>` with no `count`/`num`. It validates the
  server branch that produced the loss. The original `len=38` packet's exact
  field tags remain unresolved because its raw bytes were not retained; no
  claim of byte-for-byte reconstruction is made.

## Parser Evidence

- `JianghuOL.CBE:0x01033544 HandleItemOperationResponse`
  - subtype `4` clears the item-operation waiting flag at the scene object.
  - subtype `11/12` can update a row count by `seq`, but for ordinary items it
    only writes a count field and does not rebuild the visible list.
- `mmGameMstarWqvga.cbm:0x418C`
  - `17/1` reparses the full visible backpack item list from `iteminfo`.
  - `7/42` is the companion empty book-list object used by the backpack screen.

## 2026-07-26 Stack-count Investigation and Root Cause

### Trigger and first incorrect state

1. Put a stack of ten mooncakes in the backpack's treasure category.
2. Select the stack and choose the normal discard action once.
3. The entire stack, and therefore the only visible row in that category,
   disappears instead of the row remaining with quantity nine.

The client does not remove a category as part of `1/7/4`: saved decompilation
of `JianghuOL.CBE:0x01033544` only clears its item-operation waiting state for
subtype `4`. The authoritative list mutation is the following `17/1`, whose
parser at `mmGameMstarWqvga.cbm:0x418C` discards the old list and rebuilds it
from all `iteminfo` rows. Hence the first incorrect state is the durable
backpack decrement *before* that list response is built, not category UI
filtering or the trailing `7/11` count notification.

`vm_net_mock_build_item_discard_response()` formerly implemented this rule:

```c
discardCount = parsed.count ? parsed.count : item->count;
```

`parsed.count` is zero when no `count` or `num` field exists. Thus a normal
selector-only discard consumed the entire selected stack. The old statement
that a missing count meant "discard the whole stack" was an unsupported server
assumption and contradicted the observed client action.

### Correct contract

- A selector-only `1/7/4` request discards exactly one item.
- A positive explicit `count` or `num` discards that many items, subject to the
  normal stack bounds.
- An explicit zero count is invalid and returns the existing `result=2` failure
  response; it must not be silently converted into a full-stack removal.
- After a successful mutation, `17/1` remains the authoritative full-list
  refresh and `7/11` carries the matching selected-row remainder. They must
  agree with the same committed role state.

This is a server-side ownership correction only. No client memory, UI filter,
or response suppression is used to hide the error.

## Response Contract

On success:

```text
1/7/4  { result=1 }
1/17/1 { maxnum, iteminfo=<full active-role backpack list> }
1/7/42 { booknum=0, booksinfo=<empty> }
1/7/11 { info=<row_count=1, seq, remaining_count> }
```

On failure:

```text
1/7/4 { result=2 }
```

The role DB is saved with reason `item-discard` after the one-item or explicit
positive-count mutation. `17/1` is the authoritative visible-list refresh;
`7/11` is the CBE-side selected-row count update, generated from the same
post-mutation state.

## Runtime Validation

Expected handled source:

```text
builtin-item-discard
mock_item_discard ... request_count=default-one|explicit remaining=... \
  refresh=7/4+17/1+7/42+7/11
```

Manual checks:

1. Open backpack.
2. Discard one item from a stack of ten without an explicit count field and
   confirm that the persistent and returned count is nine.
3. Send an explicit count (for example three) and confirm only that count is
   removed.
4. Confirm no assert.
5. Confirm the item list and the bottom slot counter both reflect the active
   role backpack after discard.

## 2026-07-26 Verification

`tmp/item-discard-stack-regression.php` creates an isolated account with one
`812 月饼` row at count `10` and issues the same selector-only `1/7/4` shape
handled by the production parser. Against a freshly started local service it
verified all of the following:

```text
selector-only request: 10 -> 9, result=1,
  response=7/4 + 17/1 + 7/42 + 7/11(info seq=61001,count=9)
explicit count=3:      9 -> 6, result=1,
  response=7/4 + 17/1 + 7/42 + 7/11(info seq=61001,count=6)
explicit count=0:      6 -> 6, result=2, response=7/4 only
```

The fixture is removed after the test. `make -j2` completed before the
isolated service run.
