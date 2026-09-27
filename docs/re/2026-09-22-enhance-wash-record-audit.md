# Wash-record audit after full-DB track id (2026-09-22)

## Verdict

Core auction paint bug (claimer coalesce under colliding per-role ids) remains fixed.
**Follow-up:** wash_record is now **audit-only** — no restore/coalesce into live gear
(`docs/re/2026-09-22-wash-record-audit-only.md`).

| Path | Status |
|------|--------|
| first_seed_percent=0 / ≠0 | PASS — empty wash, no record upsert |
| Wash NPC apply | PASS |
| Equip / unequip | PASS — column wash only |
| Warehouse deposit/retrieve | PASS — no coalesce |
| Trade transfer | PASS — transfer_owner, no backfill |
| Auction list/cancel/sold/claim | PASS — claim never backfill |
| Discard/sell/ring/extract/role-delete | PASS — retire |
| Enhance transfer 29/6 | PASS — source delete + dest upsert from column |

## Earlier fixes

1. **`wash_record` PK** `(role_id,bag_seq)` → `(role_id,item_instance_id)`.
2. **upsert** requires non-zero `item_instance_id`; updates `bag_seq` on duplicate.
3. **delete** prefers instance id; bag_seq only for legacy `instance_id=0`.

## Remaining low risk

- 29/9 wash UI may read `washCount` from memory if column empty (apply path still durable).
- PK migrate drops leftover `item_instance_id=0` wash_record rows (bag/equip columns remain authority).
