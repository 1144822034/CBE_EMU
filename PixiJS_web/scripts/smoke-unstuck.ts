/**
 * Live: enter → unstuck 12/3 → expect 16/3 landing ≠ enter coords (or at least parsed).
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
import {
  buildSettingsUnstuckRequest,
  parseMmGameUnstuckLanding,
  parseServerSceneLanding,
} from "../src/net/protocolExtras";

const url = process.env.PIX_GATEWAY || "ws://127.0.0.1:19092";
const user = process.env.PIX_USER || "lxh001";
const pass = process.env.PIX_PASS || "123456";
const clientId = (Math.random() * 0xffffff) >>> 0;

function exchange(ws: WebSocket, wt: Uint8Array, timeoutMs = 10000): Promise<Uint8Array[]> {
  return new Promise((resolve, reject) => {
    const bodies: Uint8Array[] = [];
    let pending = false;
    const timer = setTimeout(() => {
      cleanup();
      if (bodies.length) resolve(bodies);
      else reject(new Error("timeout"));
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

const ws = new WebSocket(url);
await new Promise<void>((resolve, reject) => {
  const t = setTimeout(() => reject(new Error("connect timeout")), 2500);
  ws.once("open", () => {
    clearTimeout(t);
    resolve();
  });
  ws.once("error", reject);
});

const loginBodies = await exchange(ws, buildLoginAlt12Request(user, pass));
let servers = parseServerList(ingestPacket(loginBodies[0]!)!);
for (const b of loginBodies) {
  const p = ingestPacket(b);
  if (!p) continue;
  const list = parseServerList(p);
  if (list.servers.length) servers = list;
}
if (!servers.servers.length) throw new Error("no servers");
const roleBodies = await exchange(ws, buildServerSelectRequest(servers.servers[0]!.serverId));
let roles = parseRoleListFromSelect(ingestPacket(roleBodies[0]!)!);
for (const b of roleBodies) {
  const p = ingestPacket(b);
  if (!p) continue;
  const r = parseRoleListFromSelect(p);
  if (r.length) roles = r;
}
const enterBodies = await exchange(ws, buildRoleSelectRequest(roles[0]!.roleId));
await exchange(ws, buildGroupType1Request());
await exchange(ws, buildSceneStartupFollowupRequest());
let enter = null as ReturnType<typeof parseSceneEnter>;
for (const b of enterBodies) {
  const p = ingestPacket(b);
  if (!p) continue;
  enter = parseSceneEnter(p) || parseRoleSelectEnter(p) || enter;
}
if (!enter) throw new Error("no enter");
console.log("ENTER", enter.scene, `@${enter.x},${enter.y}`);

const unstuckBodies = await exchange(ws, buildSettingsUnstuckRequest(2));
let land = null as ReturnType<typeof parseMmGameUnstuckLanding>;
for (const b of unstuckBodies) {
  const p = ingestPacket(b);
  if (!p) continue;
  console.log(
    "unstuck objs",
    p.objects.map((o) => `${o.kind}/${o.subtype}`).join(", "),
  );
  land =
    parseMmGameUnstuckLanding(p) ||
    parseServerSceneLanding(p) ||
    land;
}
if (!land) throw new Error("no 16/3 landing from unstuck");
console.log("UNSTUCK", land.scene, `@${land.x},${land.y}`);
if (land.x === 0 || land.y === 0) throw new Error("landing at 0,0");
// Lin'an _06/_07 have no stone; nearest authored stone is 临安_10 @304,148.
if (/临安府_0[67]/i.test(enter.scene) || /c04.*0[67]/i.test(enter.scene)) {
  if (!/临安府_10|c04.*10/i.test(land.scene)) {
    console.warn("warn: expected telestone scene 临安_10, got", land.scene);
  }
  const dist = Math.hypot(land.x - 304, land.y - 148);
  if (dist > 48) {
    console.warn(`warn: landing far from telestone 304,148 (dist=${dist | 0})`);
  }
}
console.log("OK unstuck landing");
ws.close();
