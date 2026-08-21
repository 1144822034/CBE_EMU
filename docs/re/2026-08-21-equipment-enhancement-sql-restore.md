# 2026-08-21 Historical SQL Equipment Enhancement Restore

## Goal and authority boundary

The administration page now restores equipment enhancement state from a
historical SQL backup into the currently configured relational database.  The
backup is a source of old row values only.  The service does **not** execute the
uploaded SQL, create tables from it, or restore unrelated columns.

The only columns written are:

```text
account_role_equipment.enhance_level
account_role_equipment.enhance_affix_types
account_role_equipment.enhance_affix_values
account_role_backpack.enhance_level
account_role_backpack.enhance_affix_types
account_role_backpack.enhance_affix_values
```

## Trigger and first safety boundary

An administrator opens `/?tab=enhance-restore`, uploads a historical `.sql`
file, reviews the parsed differences, and confirms the restore.  The parser
only recognizes `INSERT INTO account_role_equipment` and
`INSERT INTO account_role_backpack`, including backtick/schema-qualified table
names, explicit column lists, and standard mysqldump row layouts.  All other SQL
text is ignored rather than executed.

The first unsafe state in a naive SQL restore would be accepting a row by item
id alone or replaying the full statement.  Both would allow a backup to alter a
different current instance or unrelated role state.  The implementation
therefore treats the current table primary key as the location and the item
identity as an additional optimistic match:

```text
equipment location: account_id, role_id, slot_index
equipment identity: item_id

backpack location:  account_id, role_id, slot_index
backpack identity:  item_id, item_seq
```

Rows whose current identity no longer matches are reported as not matched and
are never updated.  Duplicate historical rows for the same table primary key
invalidate the complete preview.

## Restore semantics

For every strictly matched instance, the historical values overwrite the
current three enhancement fields.  This is a disaster-recovery operation, not
an enhancement merge: a historical lower level is also restored when the
administrator confirms it.  A byte-for-byte identical row is skipped.

Before a preview is accepted, and again after starting the commit transaction,
every involved account must be offline.  The second check closes the preview /
confirmation race so an online role snapshot cannot immediately overwrite the
restored relational rows.  The preview token expires after 15 minutes.

Inside the transaction, each matched source row is read again with
`SELECT ... FOR UPDATE`.  If the row disappeared, its instance identity no
longer matches, or any of its three enhancement values changed since preview,
the complete restore is cancelled and rolled back.  The update statements also
retain the exact item identity predicates.

The commit uses one InnoDB transaction for every selected instance and the
audit row.  Any update/audit/commit failure rolls back the entire restore.
`server_admin_enhancement_restore_audit` records the browser-provided source
filename and source/matched/changed/missing counts; the uploaded file itself is
not persisted.

## Source validation

The parser enforces the runtime enhancement contract before any database read:

- `enhance_level` is `0..16`;
- four affix types are packed in four `u8` lanes;
- four affix values are packed in four `u16` lanes;
- zero type requires zero value;
- non-zero types are the generated stage attributes `3..10`;
- non-zero values are `1..0x7fff`;
- the four type lanes may not repeat one attribute.

Future `+4/+8/+12/+16` rolls are intentionally allowed even below their
activation level.  The client receives the complete future-stage plan when the
equipment object is created, and the normal enhancement path persists that
plan before the threshold is reached.

## Schema and migration

- `server/mysql/schema.sql` contains the audit table for new deployments.
- `server/mysql/migrate_enhancement_restore_audit.sql` adds the same table
  idempotently to existing deployments.
- The service retains `CREATE TABLE IF NOT EXISTS` at the action boundary so a
  deployment that has not yet applied the migration fails safely or creates
  only the audit table before starting the restore transaction.

## Regression and validation

The isolated parser fixture does not open MySQL or start a listener:

```text
gcc -std=gnu11 -w scripts/equipment-enhancement-sql-restore-regression.c \
  -o tmp/equipment-enhancement-sql-restore-regression.exe
tmp/equipment-enhancement-sql-restore-regression.exe

equipment enhancement SQL restore regression passed: target INSERT rows parse,
historical lower values remain overwrite candidates, and duplicate keys /
overlevel / invalid affixes are rejected
```

The production service was rebuilt with:

```text
mingw32-make -j2
```

This rebuilt `obj/server/server_main.o` and linked
`bin/jh-online-server.exe`.  No `jh_online`, `jh_online_release`, or other MySQL
database was modified during validation.  A final deployment test should use a
dedicated copy/schema, upload a small real mysqldump, verify preview differences,
commit, reload the account, and inspect the audit row and all-or-nothing rollback
behavior under a deliberately failed transaction.
