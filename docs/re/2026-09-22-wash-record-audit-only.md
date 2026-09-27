# Wash-record audit-only (no backfill)

Date: 2026-09-22  
Related: `docs/re/2026-09-22-enhance-wash-record-audit.md`,
`docs/re/2026-09-22-equipment-full-db-track-id.md`

## Contract

`account_role_enhance_wash_record` is an **audit log only**.

| Operation | Allowed |
|-----------|---------|
| upsert / relocate / transfer_owner / delete | yes (write log) |
| SELECT / restore / coalesce into bag/equip/warehouse/listing wash | **no** |

Runtime wash authority is solely the `enhance_wash` column on:

- `account_role_backpack`
- `account_role_equipment`
- `account_role_warehouse`
- auction listing / claim rows

## Change

Removed `vm_net_mock_enhance_wash_record_restore` and
`vm_net_mock_enhance_wash_coalesce_from_record`, plus all call sites that used
to fill empty in-memory wash from the record table (equip, unequip, warehouse
deposit, auction list, enhance transfer 29/6).

## Why

Coalesce under `(role_id, item_instance_id)` previously resurrected stale or
foreign wash attrs onto gear whose column was intentionally empty (e.g.
`first_seed_percent=0`, or after clear/transfer). Audit history must not drive
live equipment state.

## Verification

- `make -j2`
- Wash NPC apply still upserts a record row and persists bag/equip column.
- Equip / unequip / warehouse / trade / auction use column wash only.
- Empty column stays empty even if an old wash_record row exists for that
  instance.
