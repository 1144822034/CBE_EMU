# 2026-07-07 No-Account Login Guest Credentials

## Problem

The mock previously relied on host-only account injection
(`--mock-service-account=...`) or a server-side `default` fallback to decide
which Jianghu OL role DB bucket should receive no-account traffic.

That does not match the client contract. The title-side no-account flow already
has a packet-driven persistence path for server-issued guest credentials.

## IDA Evidence

### Request builder

`mmTitleMstarWqvga.cbm:net_build_login_request(0x1B9C)` builds:

- subtype `1/1/1` from the live login form buffers at `r9+10816` / `r9+10812`
- subtype `1/1/12` from the persisted login-record workspace at
  `mmorpg_LoginRecord + 16` (`username`) and `+48` (`password`)

So no-account `1/1/12` only sends empty credentials on a true first run, before
the client has saved anything.

### Response parser

`mmTitleMstarWqvga.cbm:net_handle_login_response(0x16DC)`:

- parses `result/serverinfo/servernum/newVer`
- for result `'3'` or `'4'`, also copies:
  - `information`
  - `username`
  - `password`
  into the local `mmorpg_LoginRecord` workspace

### Save path

`login_alt_result_dispatch(0x19C2)`:

- result `'3'` -> `sub_5916()` -> `sub_1998()` -> success dispatch
- result `'4'` -> prompt callback path `sub_5922()`

`sub_1998(0x1998)` persists the updated `mmorpg_LoginRecord` buffer and then
calls `login_record_save_default_credentials(0x1430)`, which writes the compact
`defaultLogin.dat` file containing the current username/password pair.

`login_record_init_and_load(0x2620)` reloads that persisted state on startup
and seeds the active login buffers from it.

## Packet Contract

For Jianghu OL no-account first-run login:

1. client sends `WT 1/12` with empty `username/password`
2. server responds with subtype `1/1/12` carrying:
   - `result='3'`
   - `serverinfo`
   - `color`
   - `servernum`
   - `newVer`
   - `information`
   - `username`
   - `password`
3. title parser copies the issued credentials into `mmorpg_LoginRecord`
4. callback path saves both `mmorpg_LoginRecord` and `defaultLogin.dat`
5. later no-account `1/1/12` requests reuse the saved credentials through the
   normal packet fields

## Implementation

The mock-service still:

- no longer injects account names through host metadata
- no longer falls back to a synthetic `default` account

As of 2026-10-05, empty-credential `1/1/12` no longer auto-creates `guestXXXXX`
accounts. The CBE endpoint returns login failure (`result=2`, 请先登录游戏).
Players must register via the web account center or `account create`, then send
saved credentials. See `docs/re/2026-10-05-cbe-disable-no-account-guest-register.md`.

## Validation

- `make -j2`: passed
- runtime validation of the end-to-end save/reload loop is still pending; the
  packet/IDA contract above now matches the implemented mock path
