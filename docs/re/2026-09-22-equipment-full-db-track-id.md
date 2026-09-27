# Full-DB equipment track id (`item_instance_id`)

Date: 2026-09-22  
Related: `docs/re/2026-09-22-auction-claim-wash-record-collide.md`

## Trigger

Auction claim painted wash attrs onto gear that the listing UI showed as unwashed when admin `first_seed_percent=0`. Root cause was claimer-side `wash_record` coalesce under colliding **per-role** `item_instance_id` counters, plus leftover seller/orphan wash rows after destroy paths.

## Contract

1. `item_instance_id` is **full-DB unique** for live equipment (bag / worn / warehouse / open auction listing / unclaimed item claim).
2. Allocated from `server_equipment_instance_seq` via MySQL `LAST_INSERT_ID(next_id+1)`.
3. Track id is stable across equip/unequip, warehouse, trade, and auction; it is **retired** only when the piece is destroyed (discard, NPC sell, ring submit, crystal extract) or the role is deleted.
4. On ownership change (trade commit, auction sold), wash/crystal side tables **transfer** `role_id` (and bag_seq) — they are not deleted.
5. `enhance_wash_record` is keyed by `(role_id, item_instance_id)`; `bag_seq` is metadata only. The table is **audit-only** — never SELECT/coalesce into live bag/equip/warehouse wash (see `docs/re/2026-09-22-wash-record-audit-only.md`).
6. Authority for auction claim / cancel is the listing/claim `enhance_wash` column — never coalesce under the claimer.

## Wash-record PK migrate

On first `ensure_table` after upgrade, if PRIMARY still includes `bag_seq`:

- Dedup same `(role_id, item_instance_id)`
- Drop `item_instance_id=0` legacy rows
- Switch PRIMARY KEY to `(role_id, item_instance_id)`

This stops multi-equipped / listing seats sharing `bag_seq=0` from overwriting each other's coalesce snapshots.

## Startup migrate

`vm_net_mock_equipment_instance_migrate_once` (from `mysql_authority_prepare`):

- Ensures `account_role_warehouse.item_instance_id`.
- Scans live rows; keeps one keeper per colliding old id; reallocates the rest; zero-fills missing ids.
- Collision rule: first row in a sorted same-`oldInstanceId` group keeps the id; **every later row** in that group gets a new id (not only the immediate next after the keeper).
- Orphan-deletes wash/crystal rows with no live location (including warehouse).
- Sealed by `server_equipment_instance_migrate.remap_done=1`.

## Code map

| Area | Location |
|------|----------|
| Alloc / retire / transfer / migrate | `src/server/mock_server_equipment_instance.inc.c` |
| Ensure + first obtain | `vm_net_mock_role_ensure_equipment_instance_id` / `add_backpack_item_to_role` |
| Warehouse R/W | `vm_net_mock_warehouse_load_row` / `warehouse_write_sql` |
| Discard / sell / ring / extract / role-delete | catalog / role / activities / equipment_npc |
| Trade / auction sold transfer | guild trade commit / `vm_mock_auction_settle_listing` |
| No starter weapon | `vm_net_mock_role_init_default_equipment` |

## Verification

- `make -j2`
- New role: empty worn slots; first equip grant gets non-zero global instance id.
- Discard / NPC sell / ring equip submit: wash_record + crystal_spent rows gone for that instance.
- Trade / auction win+claim: same instance id; wash/crystal follow buyer `role_id`.
- Warehouse deposit/retrieve: `item_instance_id` persists; wash_record bag_seq relocates.

## Unresolved

- Historical non-equipment bag rows may receive track ids during remap zero-fill (harmless).
- `role->nextEnhanceInstanceId` remains updated for diagnostics but is unused for gear allocation.
