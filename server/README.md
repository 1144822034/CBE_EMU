# Local Mock Service

The mock path is now service-first:

- mock logic is exposed through a TCP service (default bind is localhost)
- emulator mode acts as a client of that service
- when the service is unavailable, the emulator no longer exits; requests simply
  stop receiving mock responses and the game can follow its own timeout path

## Start the service

### Linux

Linux builds only the headless service executable. Install the compiler first
(package name shown for Debian/Ubuntu):

```bash
apt-get install build-essential
make -j2
./bin/jh-online-server --mock-service-bind=0.0.0.0 --mock-service-port=19090
```

The Linux target does not require SDL or Unicorn and does not build or start
the emulator client. MySQL connection settings remain controlled by the `CBE_MYSQL_*`
environment variables documented under `server/mysql`.

### Windows

```powershell
bin/main.exe --mock-service-only --mock-service-port=19090
```

You can also use:

```powershell
bin/main.exe --mock-service-only --mock-service-bind=127.0.0.1 --mock-service-port=19090
```

To expose the service on a public server:

```powershell
bin/main.exe --mock-service-only --mock-service-bind=0.0.0.0 --mock-service-port=19090
```

Or bind a specific NIC/public IPv4:

```powershell
bin/main.exe --mock-service-only --mock-service-bind=203.0.113.10 --mock-service-port=19090
```

If you start from the repo root, the runtime will automatically switch into
`./bin` when it detects that the CBE and font assets live there.

## Start the emulator as a client

By default the emulator expects the service at `127.0.0.1:19090`.

```powershell
bin/main.exe
```

To use another port:

```powershell
bin/main.exe --mock-service=127.0.0.1:19190
```

To connect to a remote public server:

```powershell
bin/main.exe --mock-service=203.0.113.10:19090
```

For no-account title flows such as Jianghu OL `1/1/12` empty-credential login,
the first visible request is only the server-list preflight. When the request
does not carry saved credentials yet, the service now issues a guest
`username/password` pair in the login response so the client can persist it into
its own `mmorpg_LoginRecord` / `defaultLogin.dat` flow. Later no-account
requests reuse those saved credentials through the normal packet fields instead
of a host-side injected account override.

Startup pre-login version/update handshake packets (`WT 18/*`) and the short
login-bridge control ack (`WT 99/1`) still happen before any account exists.
Those requests are handled statelessly, so the game can leave the
"与服务器通讯 / 获取版本信息" phase and reach the login/title flow.

For credential login flows, the server does not auto-create accounts from the
packet anymore: the login `username` / `userName` plus `password` must already
exist in the server-side account DB or the mock returns an account/password
error.

After the service starts, you can manage login accounts directly in the server
console:

```text
help
account list
account create <username> <password>
account passwd <username> <newpassword>
```

Server-side role data now stores per account:

- guest / named account: `nvram/accounts/<account>/jhol_mock_roles.bin`

Besides the emulator arguments, the server side still needs its firewall, cloud
security-group, or NAT port-forwarding rules to allow inbound TCP traffic on
that port.

If the service is unavailable, the emulator still starts. Later requests simply
do not receive mock responses, so the game can enter its own network timeout
flow instead of the host exiting early.

## Legacy options

The old `--mock-service-port=` and `--mock-service-remote=` options are still
accepted as aliases while scripts are being updated, but the runtime behavior is
already service-only.

## Frame format

Every TCP connection carries one request and one response.

Request header, 20 bytes, little-endian:

1. magic: `"CBMS"`
2. version: `1`
3. flags: bit `0x1` means ping
4. payload length
5. aux: request metadata length in bytes

Then optional request metadata follows:

1. `u32 clientId`

Then the raw WT request bytes follow.

Response header, 20 bytes, little-endian:

1. magic: `"CBMR"`
2. version: `1`
3. flags: bit `0x1` means `closeAfterData`
4. payload length
5. aux: response event type

Then the raw mock response bytes follow.
