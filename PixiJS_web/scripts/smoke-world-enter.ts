/**
 * Live path: login → server → first role → dump enter + follow-up objects.
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
  buildBattleStartRequest,
  ingestPacket,
  parseNearbyPeers,
  parseNpcCatalog,
  parseRoleListFromSelect,
  parseRoleSelectEnter,
  parseSceneEnter,
  parseServerList,
  parseBattleStart,
  parseSceneMoveSeeds,
} from "../src/net/protocol";
import {
  buildMoveRequest,
  buildBattleEscapeRequest,
  parseBattleAction,
  parseSceneReload,
} from "../src/net/protocolExtras";

const url = process.env.PIX_GATEWAY || "ws://127.0.0.1:19092";
const clientId = (Math.random() * 0xffffff) >>> 0;

function exchange(ws: WebSocket, wt: Uint8Array, timeoutMs = 10000): Promise<Uint8Array[]> {
  return new Promise((resolve, reject) => {
    const bodies: Uint8Array[] = [];
    let pending = false;
    const timer = setTimeout(() => {
      cleanup();
      if (bodies.length) resolve(bodies);
      else reject(new Error("exchange timeout"));
    }, timeoutMs);
    const onMsg = (data: WebSocket.RawData) => {
      const buf = data instanceof Buffer ? new Uint8Array(data) : new Uint8Array(data as ArrayBuffer);
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

function dump(label: string, bodies: Uint8Array[]) {
  console.log(`\n=== ${label} bodies=${bodies.length} ===`);
  for (const [i, b] of bodies.entries()) {
    const p = ingestPacket(b);
    if (!p) {
      console.log(i, "not wt", b.length);
      continue;
    }
    console.log(
      i,
      p.objects.map((o) => `${o.kind}/${o.subtype}`).join(", "),
    );
    const enter = parseSceneEnter(p) || parseRoleSelectEnter(p);
    if (enter) console.log("  ENTER", enter);
    const reload = parseSceneReload(p);
    if (reload) console.log("  RELOAD", reload);
    const npcs = parseNpcCatalog(p);
    if (npcs.length) console.log("  NPC", npcs.length, npcs.slice(0, 2).map((n) => n.name));
    const peers = parseNearbyPeers(p);
    if (peers.length) console.log("  PEERS", peers.length);
  }
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
console.log("creds", user, pass ? "***" : "(empty)");
const loginBodies = await exchange(ws, buildLoginAlt12Request(user, pass));
dump("login", loginBodies);
let servers = {
  servers: [] as ReturnType<typeof parseServerList>["servers"],
  username: "",
  password: "",
};
for (const b of loginBodies) {
  const p = ingestPacket(b);
  if (!p) continue;
  const list = parseServerList(p);
  console.log("parseServerList", list.result, list.servers.length, list.information);
  if (list.servers.length)
    servers = { ...list, username: list.username ?? "", password: list.password ?? "" };
}
if (!servers.servers.length) throw new Error("no servers");
const sid = servers.servers[0]!.serverId;

const roleBodies = await exchange(ws, buildServerSelectRequest(sid));
dump("server-select", roleBodies);
let roles = [] as ReturnType<typeof parseRoleListFromSelect>;
for (const b of roleBodies) {
  const p = ingestPacket(b);
  if (!p) continue;
  const r = parseRoleListFromSelect(p);
  if (r.length) roles = r;
}
if (!roles.length) throw new Error("no roles — guest has empty list?");
console.log(
  "roles",
  roles.map((r) => `${r.roleId}:${r.name}:L${r.level}`),
);

const enterBodies = await exchange(ws, buildRoleSelectRequest(roles[0]!.roleId));
dump("role-select", enterBodies);

const groupBodies = await exchange(ws, buildGroupType1Request());
dump("group-type1", groupBodies);

const followBodies = await exchange(ws, buildSceneStartupFollowupRequest());
dump("scene-followup", followBodies);

let enter = null as ReturnType<typeof parseSceneEnter>;
let npcCount = 0;
for (const b of [...enterBodies, ...groupBodies, ...followBodies]) {
  const p = ingestPacket(b);
  if (!p) continue;
  enter = parseSceneEnter(p) || parseRoleSelectEnter(p) || enter;
  npcCount += parseNpcCatalog(p).length;
}
if (!enter?.scene) throw new Error("no enter scene after role+followup");
console.log("OK enter", enter.scene, "@", enter.x, enter.y, "npc≈", npcCount);

const dirs = new Uint8Array([2, 2, 2, 2, 2, 3, 3, 3, 3, 3]);
const moveBodies = await exchange(ws, buildMoveRequest(dirs));
dump("move-2/1", moveBodies);

const px = enter.x | 0;
const py = enter.y | 0;
const battleBodies = await exchange(ws, buildBattleStartRequest(9001, 0, px, py));
dump("battle-4/1", battleBodies);
let battled = false;
for (const b of battleBodies) {
  const p = ingestPacket(b);
  if (!p) continue;
  const st = parseBattleStart(p, { roleName: "smoke" });
  if (st) {
    battled = true;
    const seeds = parseSceneMoveSeeds(p);
    console.log(
      "  BATTLE",
      st.subtype,
      "L",
      st.left.map((u) => `${u.name}:${u.hp}/${u.maxHp}`),
      "R",
      st.right.map((u) => `${u.name}:${u.hp}/${u.maxHp}`),
      "seeds",
      seeds.map((s) => `${s.actorId}:${s.hp}/${s.hpMax}`),
    );
  }
}
if (!battled) console.warn("WARN: no parseBattleStart from 4/1 (check id/scene)");

const escapeBodies = await exchange(ws, buildBattleEscapeRequest());
dump("battle-escape-4/4", escapeBodies);
for (const b of escapeBodies) {
  const p = ingestPacket(b);
  if (!p) continue;
  const up = parseBattleAction(p);
  if (up) console.log("  ACTION", up);
}

ws.close();
console.log("\nDONE");
