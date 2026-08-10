/**
 * Live: login → enter → hangup 2/10 Type=2+25/3 → dump response.
 */
import WebSocket from "ws";
import {
  CBMR_FLAG_HAS_FOLLOWUP,
  decodeCbmr,
  encodeCbms,
  encodeRequestMeta,
} from "../src/net/cbms";
import {
  buildGroupType1Request,
  buildLoginAlt12Request,
  buildRoleSelectRequest,
  buildSceneStartupFollowupRequest,
  buildServerSelectRequest,
  ingestPacket,
  parseRoleListFromSelect,
  parseRoleSelectEnter,
  parseSceneEnter,
  parseServerList,
  parseBattleStart,
} from "../src/net/protocol";
import { buildHangupStartRequest } from "../src/net/protocolExtras";

const url = process.env.PIX_GATEWAY || "ws://127.0.0.1:19092";
const clientId = (Math.random() * 0xffffff) >>> 0;

function exchange(
  ws: WebSocket,
  wt: Uint8Array,
  timeoutMs = 10000,
): Promise<Uint8Array[]> {
  return new Promise((resolve, reject) => {
    const bodies: Uint8Array[] = [];
    let pending = false;
    const timer = setTimeout(() => {
      cleanup();
      if (bodies.length) resolve(bodies);
      else reject(new Error("exchange timeout"));
    }, timeoutMs);
    const onMsg = (data: WebSocket.RawData) => {
      const buf =
        data instanceof Buffer
          ? new Uint8Array(data)
          : new Uint8Array(data as ArrayBuffer);
      try {
        const frame = decodeCbmr(buf);
        if (frame.body.length) bodies.push(frame.body);
        pending = (frame.flags & CBMR_FLAG_HAS_FOLLOWUP) !== 0;
        if (!pending) {
          cleanup();
          resolve(bodies);
        }
      } catch (e) {
        cleanup();
        reject(e);
      }
    };
    const cleanup = () => {
      clearTimeout(timer);
      ws.off("message", onMsg);
    };
    ws.on("message", onMsg);
    ws.send(Buffer.from(encodeCbms(0, encodeRequestMeta(clientId), wt)));
  });
}

function dumpChat(bodies: Uint8Array[]) {
  for (const b of bodies) {
    const p = ingestPacket(b);
    if (!p) continue;
    console.log(
      "objects",
      p.objects.map((o) => `${o.kind}/${o.subtype}`).join(", "),
    );
    for (const o of p.objects) {
      if (o.kind === 3 && o.subtype === 3) {
        const { findField, decodeFieldString } = awaitImport();
        void findField;
      }
    }
  }
}

function awaitImport() {
  return {};
}

const ws = new WebSocket(url);
await new Promise<void>((resolve, reject) => {
  const t = setTimeout(() => reject(new Error("connect timeout")), 2500);
  ws.once("open", () => {
    clearTimeout(t);
    resolve();
  });
  ws.once("error", reject);
});

const user = process.env.PIX_USER || "lxh001";
const pass = process.env.PIX_PASS || "123456";
console.log("gateway", url, "user", user);

const loginBodies = await exchange(ws, buildLoginAlt12Request(user, pass));
let servers = parseServerList(ingestPacket(loginBodies[0]!)!);
for (const b of loginBodies) {
  const p = ingestPacket(b);
  if (!p) continue;
  const list = parseServerList(p);
  if (list.servers.length) servers = list;
}
if (!servers.servers.length) throw new Error("no servers");
const sid = servers.servers[0]!.serverId;

const roleBodies = await exchange(ws, buildServerSelectRequest(sid));
let roles: ReturnType<typeof parseRoleListFromSelect> = [];
for (const b of roleBodies) {
  const p = ingestPacket(b);
  if (!p) continue;
  const r = parseRoleListFromSelect(p);
  if (r.length) roles = r;
}
if (!roles.length) throw new Error("no roles");
console.log("role", roles[0]!.roleId, roles[0]!.name);

const enterBodies = await exchange(
  ws,
  buildRoleSelectRequest(roles[0]!.roleId),
);
await exchange(ws, buildGroupType1Request());
await exchange(ws, buildSceneStartupFollowupRequest());

let scene = "?";
for (const b of enterBodies) {
  const p = ingestPacket(b);
  if (!p) continue;
  const e = parseSceneEnter(p) || parseRoleSelectEnter(p);
  if (e?.scene) scene = e.scene;
}
console.log("scene", scene);

const hangupWt = buildHangupStartRequest();
console.log(
  "hangup wt",
  hangupWt.length,
  [...hangupWt].map((x) => x.toString(16).padStart(2, "0")).join(" "),
);
const hangBodies = await exchange(ws, hangupWt);
console.log("\n=== hangup response bodies=", hangBodies.length, "===");
for (const [i, b] of hangBodies.entries()) {
  const p = ingestPacket(b);
  if (!p) {
    console.log(i, "not wt", b.length);
    continue;
  }
  console.log(
    i,
    p.objects.map((o) => `${o.kind}/${o.subtype}`).join(", "),
  );
  const battle = parseBattleStart(p, { roleName: roles[0]!.name });
  if (battle) {
    console.log(
      "  BATTLE subtype",
      battle.subtype,
      "left",
      battle.left.map((u) => u.name),
    );
  }
  // scan strings for tip
  const ascii = Buffer.from(b).toString("binary");
  if (ascii.includes("\xb9\xa6\xc4\xdc\xd4\xdd\xce\xb4\xca\xb5\xcf\xd6")) {
    console.log("  TIP: 功能暂未实现");
  }
  if (ascii.includes("\xb9\xd2\xbb\xfa")) {
    console.log("  TIP: contains 挂机");
  }
  if (ascii.includes("\xb2\xbb\xc4\xdc\xb9\xd2\xbb\xfa")) {
    console.log("  TIP: 不能挂机");
  }
}

ws.close();
console.log("done");
void dumpChat;
