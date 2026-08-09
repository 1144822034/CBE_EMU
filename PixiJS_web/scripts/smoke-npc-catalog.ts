/**
 * Verify post-enter 25/5+2/3+27/11+7/42 yields nonempty NPC catalog.
 */
import WebSocket from "ws";
import {
  CBMR_FLAG_HAS_FOLLOWUP,
  decodeCbmr,
  encodeCbms,
  encodeRequestMeta,
} from "../src/net/cbms.ts";
import {
  buildGroupType1Request,
  buildLoginAlt12Request,
  buildRoleSelectRequest,
  buildSceneChangePostEnterFollowupRequest,
  buildSceneStartupFollowupRequest,
  buildServerSelectRequest,
  ingestPacket,
  packetHasNpcCatalog,
  parseNpcCatalog,
  parseRoleListFromSelect,
  parseServerList,
} from "../src/net/protocol.ts";
import {
  buildSceneChangeRequest,
  parseSceneReload,
} from "../src/net/protocolExtras.ts";

const url = process.env.PIX_GATEWAY || "ws://127.0.0.1:19092";
const user = process.env.PIX_USER || "lxh001";
const pass = process.env.PIX_PASS || "123456";
const target = process.env.PIX_TARGET || "c00蓬莱仙岛_01.sce";
const clientId = (Math.random() * 0xffffff) >>> 0;

function ex(ws: WebSocket, wt: Uint8Array, timeoutMs = 12000) {
  return new Promise<Uint8Array[]>((resolve, reject) => {
    const bodies: Uint8Array[] = [];
    const timer = setTimeout(() => {
      cleanup();
      bodies.length ? resolve(bodies) : reject(new Error("timeout"));
    }, timeoutMs);
    const onMsg = (data: WebSocket.RawData) => {
      const buf =
        data instanceof Buffer ? new Uint8Array(data) : new Uint8Array(data as ArrayBuffer);
      const frame = decodeCbmr(buf);
      if (frame.body.length) bodies.push(frame.body);
      if ((frame.flags & CBMR_FLAG_HAS_FOLLOWUP) === 0) {
        cleanup();
        resolve(bodies);
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
  for (const b of bodies) {
    const p = ingestPacket(b);
    if (!p) continue;
    const npcs = parseNpcCatalog(p);
    console.log(
      label,
      p.objects.map((o) => `${o.kind}/${o.subtype}`).join(","),
      "has27=" + packetHasNpcCatalog(p),
      "npc×" + npcs.length,
      npcs
        .slice(0, 4)
        .map((n) => `${n.name}@${n.x},${n.y}`)
        .join(" ") || "-",
      parseSceneReload(p)?.scene || "",
    );
    return npcs;
  }
  return [];
}

const ws = new WebSocket(url);
await new Promise<void>((r, j) => {
  ws.once("open", () => r());
  ws.once("error", j);
});

const login = await ex(ws, buildLoginAlt12Request(user, pass));
let sid = 0;
for (const b of login) {
  const p = ingestPacket(b);
  if (!p) continue;
  const s = parseServerList(p);
  console.log("login", s.result, s.information, "servers", s.servers.length);
  if (s.servers[0]) sid = s.servers[0].serverId;
}
if (!sid) {
  ws.close();
  throw new Error("login failed — check PIX_USER/PIX_PASS / MySQL");
}

const rb = await ex(ws, buildServerSelectRequest(sid));
let roles = [] as ReturnType<typeof parseRoleListFromSelect>;
for (const b of rb) {
  const p = ingestPacket(b);
  if (!p) continue;
  const r = parseRoleListFromSelect(p);
  if (r.length) roles = r;
}
await ex(ws, buildRoleSelectRequest(roles[0]!.roleId));
await ex(ws, buildGroupType1Request());
const startupNpcs = dump("startup", await ex(ws, buildSceneStartupFollowupRequest()));
if (!startupNpcs.length) throw new Error("startup missing NPCs");

dump("change", await ex(ws, buildSceneChangeRequest(target, 1)));
const followNpcs = dump(
  "post-enter",
  await ex(ws, buildSceneChangePostEnterFollowupRequest(target, 1)),
);
if (!followNpcs.length) throw new Error(`post-enter missing NPCs for ${target}`);

console.log("OK npc-post-enter");
ws.close();
