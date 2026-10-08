# 2026-07-07 Mock Service Account Isolation

## Problem

After the mock moved into TCP service mode, all remote clients still shared one
process-global Jianghu OL runtime:

- one shared default-account role DB file at the legacy root path:
  `nvram/jhol_mock_roles.bin`
- one active role pointer
- one battle/session/shop/title transient state set

That was acceptable for a single local loopback client, but it broke remote
multi-user deployment because different accounts could read and overwrite each
other's role rows, backpack, scene, and battle state.

## Current Contract

The mock service now carries only a per-connection `clientId` in the outer TCP
wrapper. Account selection is packet-driven: login packets bind a session to an
account, and only then does the service restore a dedicated runtime context.

Request frame:

```text
magic   "CBMS"
ver     1
flags   bit0 = ping
len     metadata_len + wt_request_len
aux     metadata_len
body    metadata bytes, then raw WT request bytes
```

Current metadata payload:

```text
u32 clientId
```

Response frame is unchanged:

```text
magic   "CBMR"
ver     1
flags   bit0 = closeAfterData
len     raw response length
aux     response event type
body    raw WT response bytes
```

## Account Selection

Client-side / service-side priority:

1. authenticated login packet credentials (`username` / `userName` + `password`)
2. authenticated server-side session binding for that `clientId`
3. Jianghu OL no-account `1/1/12` with empty credentials is rejected; it does
   not auto-register a guest account

Requests that legitimately happen before account binding, such as startup
version/update handshake packets (`WT 18/*`) and the login-bridge short control
ack (`WT 99/1`), are handled statelessly and do not create or attach an account
context.

In Jianghu OL specifically, the title `1/1/12` request is built from the local
`mmorpg_LoginRecord` username/password slots. A truly first-run no-account flow
therefore sends empty credentials. That first-run packet is now a login
failure, not an auto-issued guest. Existing clients that already persisted
credentials still log in through the normal username/password fields.

For login packets that do carry non-empty `username` / `userName` plus
`password`, the service no longer auto-creates or auto-learns accounts. The
credentials must match a server-side account DB entry or the mock returns login
failure immediately.

Account DB path:

```text
nvram/mock_service_accounts.bin
```

Server console commands:

```text
help
account list
account create <username> <password>
account passwd <username> <newpassword>
```

## Implementation

`src/mock-server.c` now keeps a linked list of per-account service contexts.
Each context snapshots the mutable Jianghu OL server state that used to be
process-global, including:

- role DB load/valid bits and `vm_net_mock_role_db_file`
- title/server-select/shop transient flags
- battle serials, HP/MP, enemy-slot state, reward serials
- scene transfer / teleport / moveinfo / reload tracking
- pending scene-save bridge state used by scene enter follow-ups
- update chunk progress fields used by staged enter-game loading

The service request path:

1. reads `clientId` from the TCP frame metadata
2. if the WT request is a credential login, validates username/password against
   the account DB
3. if the WT request is no-account `1/1/12` with empty credentials, return
   login failure and do not create or bind an account
4. if the WT request is a startup pre-login packet (`WT 18/*`), handles it
   statelessly with no bound account
5. otherwise binds `clientId -> username`
6. resolves the current account from the bound session
7. finds or creates that account context
8. restores its saved runtime globals into the existing mock code
9. processes the WT request normally
10. captures the mutated globals back into that account context

This keeps the existing packet builders and parsers unchanged while isolating
state per account.

## Persistence Path

- guest / named account: `nvram/accounts/<sanitized-account>/jhol_mock_roles.bin`

The path sanitizer keeps `[0-9A-Za-z_-]` and rewrites other bytes to `_`.

## Validation

- `make -j2`: passed
- service-mode smoke:
  - `--mock-service-only --mock-service-bind=127.0.0.1 --mock-service-port=19193`
  - manual frame injection logged account/session binding after login WT dispatch

## Notes

- This pass isolates server-side state per account, not per simultaneous device
  session. Two live clients using the same account intentionally share one mock
  account state.
- No client/game WT packet contract was changed; only the outer mock-service TCP
  wrapper carries `clientId` so the service can keep session bindings.
