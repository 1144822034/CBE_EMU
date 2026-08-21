# 2026-08-21 Equipment Enhancement Persistence Boundary

## Phase

```text
phase: equipment-enhance-commit
status: validated
request: 1/29/3 (equipseq + occultinfo)
response: 1/29/3 result
```

## First deviation and root cause

The enhancement commit path in `src/server/mock_server_equipment_npc.c` used to
deduct crystals and copper, mutate `enhanceLevel`/`enhanceAffixes`, call
`vm_net_mock_role_db_save(...)`, ignore its boolean return value, and always send
the precomputed success or random-failure result.  The role save is a real
InnoDB transaction and can fail after the in-memory role has already changed.

That produced the reported split state:

```text
current session: level/affixes and consumed resources appear changed
database:       transaction rolled back to the old instance
next login:     old level and old affixes are loaded
```

The same bug can affect an ordinary failed roll: the database save failure was
ignored even though the client received `result=2`.

Current production artifacts did not contain a matching
`mock_role_db_mysql_save_failed ... reason=equipment-enhance...` line, so the
runtime failure rate cannot be reconstructed from the available log.  The
contract violation is nevertheless conclusive from the write path and the
existing transfer implementation, which already rolls back a complete role
snapshot when its save callback fails.

## Protocol and parser evidence

- `江湖OL.CBE:HandleItemUseAndEquip(0x01028C7C)` reads `1/29/3.result` first.
- Results `1` and `2` enter the material/sequence update branch.
- Results `3`, `4`, `5`, and `6` are native business errors: equipment missing,
  copper insufficient, crystal insufficient, and level cap respectively.
- Any other result, including `0`, reaches the cancel path at approximately
  `0x010293B8..0x0102940C` and does not apply the client-side material-consuming
  success/failure branch.  It is therefore the narrow response for a server
  persistence failure; it must not be replaced by random failure (`2`).

The relational role save writes the instance fields in both authoritative
tables:

```text
account_role_equipment.enhance_level
account_role_equipment.enhance_affix_types
account_role_equipment.enhance_affix_values
account_role_backpack.enhance_level
account_role_backpack.enhance_affix_types
account_role_backpack.enhance_affix_values
```

## Fix

`vm_net_mock_equipment_enhance_persist_or_rollback()` now owns the enhancement
save boundary.  Before any crystal consumption or copper/instance mutation,
the handler copies the complete `vm_net_mock_role_state`.  After the mutation,
it checks the save result:

- save succeeds: level, generated/preserved affixes, materials, and copper are
  committed together;
- save fails: the complete role snapshot is restored and the response sends
  `result=0`, with `persistence-failed` evidence in both service and autotest
  logs.

The existing durable-save reason labels remain stable:
`equipment-enhance-success` for a successful roll and
`equipment-enhance-failed` for a random failed roll.  This keeps existing
production diagnostics usable while making the boolean save result authoritative.

Material-consumption failure also restores the snapshot so a partial duplicate
material request cannot consume an earlier row before returning crystal
insufficient.

## Regression

The isolated server-only fixture
`scripts/equipment-enhancement-persistence-regression.c` invokes the production
save/rollback helper with explicit success and failure callbacks.  It verifies:

- a successful commit keeps +4, a newly generated affix, the consumed crystal,
  and copper deduction;
- a failed save restores the role byte-for-byte, including level, all affixes,
  materials, copper, and unrelated role fields;
- the save callback is called exactly once with the stable success/failure reason
  label.

Command and result:

```text
gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 \
  -ffunction-sections -fdata-sections -w \
  scripts/equipment-enhancement-persistence-regression.c \
  obj/server/gifDecode.o obj/server/mystd.o obj/server/mysql-client.o \
  obj/server/md5.o '-Wl,--gc-sections' \
  -o tmp/equipment-enhancement-persistence-regression.exe \
  -lpthread -liconv -lm -lkernel32 -lws2_32
./tmp/equipment-enhancement-persistence-regression.exe

equipment enhancement persistence regression passed: success commits
level/affixes/materials/money and save failure restores the complete role snapshot
```

## Validation boundary

- `make -j2` passed after the implementation.
- `git diff --check` passed.
- The persistence fixture and the adjacent transfer regression both passed.
- A production-like isolated MySQL run could additionally force a save failure and verify
  the wire `29/3 result=0`, the rollback log, and unchanged rows after reload.
- No production `jh_online` data was written by the regression fixture.
