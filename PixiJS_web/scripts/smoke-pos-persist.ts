/**
 * Live: enter → walk right → reconnect → compare actorinfo x/y.
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
} from "../src/net/protocol";
import { buildMoveRequest } from "../src/net/protocolExtras";

const url = process.env.PIX_GATEWAY || "ws://127.0.0.1:19092";
const user = process.env.PIX_USER || "lxh001";
const pass = process.env.PIX_PASS || "123456";

function exchange(
  ws: WebSocket,
  clientId: number,
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
        data instanceof Buffer ? new Uint8Array(data) : new Uint8Array(data as ArrayBuffer);
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

function enterFrom(bodies: Uint8Array[]) {
  for (const b of bodies) {
    const p = ingestPacket(b);
    if (!p) continue;
    const e = parseSceneEnter(p) || parseRoleSelectEnter(p);
    if (e) return e;
  }
  return null;
}

async function openSession(label: string) {
  const clientId = (Math.random() * 0xffffff) >>> 0;
  const ws = new WebSocket(url);
  await new Promise<void>((resolve, reject) => {
    const t = setTimeout(() => reject(new Error("connect timeout")), 2500);
    ws.once("open", () => {
      clearTimeout(t);
      resolve();
    });
    ws.once("error", reject);
  });
  const loginBodies = await exchange(ws, clientId, buildLoginAlt12Request(user, pass));
  let servers: ReturnType<typeof parseServerList> = {
    servers: [],
    result: 0,
    information: "",
  };
  for (const b of loginBodies) {
    const p = ingestPacket(b);
    if (!p) {
      console.log(label, "login body not wt", b.length);
      continue;
    }
    const list = parseServerList(p);
    console.log(
      label,
      "login",
      p.objects.map((o) => `${o.kind}/${o.subtype}`).join(","),
      "result",
      list.result,
      "servers",
      list.servers.length,
      list.information,
    );
    if (list.servers.length) servers = list;
  }
  if (!servers.servers.length) throw new Error(`${label}: no servers`);
  const roleBodies = await exchange(
    ws,
    clientId,
    buildServerSelectRequest(servers.servers[0]!.serverId),
  );
  let roles = parseRoleListFromSelect(ingestPacket(roleBodies[0]!)!);
  for (const b of roleBodies) {
    const p = ingestPacket(b);
    if (!p) continue;
    const r = parseRoleListFromSelect(p);
    if (r.length) roles = r;
  }
  if (!roles.length) throw new Error(`${label}: no roles`);
  const enterBodies = await exchange(ws, clientId, buildRoleSelectRequest(roles[0]!.roleId));
  await exchange(ws, clientId, buildGroupType1Request());
  await exchange(ws, clientId, buildSceneStartupFollowupRequest());
  const enter = enterFrom(enterBodies);
  if (!enter) throw new Error(`${label}: no enter`);
  console.log(label, "ENTER", enter.scene, `@${enter.x},${enter.y}`, "role", roles[0]!.name);
  return { ws, clientId, enter };
}

const a = await openSession("A");
await new Promise((r) => setTimeout(r, 1200));
for (let i = 0; i < 8; i++) {
  await exchange(a.ws, a.clientId, buildMoveRequest(new Uint8Array(10).fill(2)));
  await new Promise((r) => setTimeout(r, 1100));
}
console.log("A walked right ×8 packets (80 steps @4px ≈ +320 if all accepted)");
a.ws.close();
await new Promise((r) => setTimeout(r, 800));

const b = await openSession("B");
const dx = (b.enter.x | 0) - (a.enter.x | 0);
const dy = (b.enter.y | 0) - (a.enter.y | 0);
console.log("DELTA", { before: [a.enter.x, a.enter.y], after: [b.enter.x, b.enter.y], dx, dy });
if (dx === 0 && dy === 0) {
  console.error("FAIL: position unchanged after move+relogin");
  process.exitCode = 1;
} else {
  console.log("OK: position changed");
}
b.ws.close();
