/**
 * Reproduce world/region map-stone → check 27/11 after land + 2/3 + 6/1.
 */
import WebSocket from "ws";
import {
  CBMR_FLAG_HAS_FOLLOWUP,
  CBMS_FLAG_SCENE_POLL,
  decodeCbmr,
  encodeCbms,
  encodeRequestMeta,
} from "../src/net/cbms.ts";
import {
  buildGroupType1Request,
  buildLoginAlt12Request,
  buildRoleSelectRequest,
  buildSceneNpcCatalogFollowupRequest,
  buildSceneStartupFollowupRequest,
  buildServerSelectRequest,
  ensureSceneFile,
  ingestPacket,
  packetHasNpcCatalog,
  parseNpcCatalog,
  parseRoleListFromSelect,
  parseSceneEnter,
  parseServerList,
} from "../src/net/protocol.ts";
import {
  buildBagOpenRequest,
  buildMapTeleportConfirmedExitRequest,
  buildMapTeleportPrepareRequest,
  buildSceneChangeRequest,
  parseMapTeleportConfirm,
  parseBagItems,
  TELEPORT_STONE_ITEM_ID,
} from "../src/net/protocolExtras.ts";

const url = process.env.PIX_GATEWAY || "ws://127.0.0.1:19092";
const user = process.env.PIX_USER || "lxh001";
const pass = process.env.PIX_PASS || "123456";
const cur = Number(process.env.PIX_CUR || "4");
const obj = Number(process.env.PIX_OBJ || "1");
const exitId = Number(process.env.PIX_EXIT || "1");
const expect = ensureSceneFile(process.env.PIX_TARGET || "c04临安府_01.sce");
const clientId = (Math.random() * 0xffffff) >>> 0;

const sleep = (ms: number) => new Promise<void>((r) => setTimeout(r, ms));

function exchange(
  ws: WebSocket,
  wt: Uint8Array,
  flags = 0,
  timeoutMs = 12000,
): Promise<Uint8Array[]> {
  return new Promise((resolve, reject) => {
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
    ws.send(Buffer.from(encodeCbms(flags, encodeRequestMeta(clientId), wt)));
  });
}

function dump(label: string, bodies: Uint8Array[]) {
  let npcs: ReturnType<typeof parseNpcCatalog> = [];
  let scene = "";
  const kinds: string[] = [];
  for (const b of bodies) {
    const p = ingestPacket(b);
    if (!p) {
      kinds.push(`raw${b.length}`);
      continue;
    }
    for (const o of p.objects) kinds.push(`${o.kind}/${o.subtype}`);
    if (packetHasNpcCatalog(p)) npcs = parseNpcCatalog(p);
    const e = parseSceneEnter(p);
    if (e) scene = e.scene;
  }
  console.log(
    label,
    "kinds=",
    kinds.join(",") || "(empty)",
    "scene=",
    scene || "-",
    "npc×",
    npcs.length,
    npcs
      .slice(0, 5)
      .map((n) => `${n.name}@${n.x},${n.y}`)
      .join(" ") || "-",
  );
  return { npcs, scene, kinds };
}

const ws = new WebSocket(url);
await new Promise<void>((r, j) => {
  ws.once("open", () => r());
  ws.once("error", j);
});

const login = await exchange(ws, buildLoginAlt12Request(user, pass));
let sid = 0;
for (const b of login) {
  const p = ingestPacket(b);
  if (!p) continue;
  const s = parseServerList(p);
  if (s.servers[0]) sid = s.servers[0].serverId;
}
if (!sid) throw new Error("login failed");
const rb = await exchange(ws, buildServerSelectRequest(sid));
let roles = [] as ReturnType<typeof parseRoleListFromSelect>;
for (const b of rb) {
  const p = ingestPacket(b);
  if (!p) continue;
  const r = parseRoleListFromSelect(p);
  if (r.length) roles = r;
}
await exchange(ws, buildRoleSelectRequest(roles[0]!.roleId));
dump("startup", await exchange(ws, buildSceneStartupFollowupRequest()));
await exchange(ws, buildGroupType1Request());

const bag = await exchange(ws, buildBagOpenRequest());
let stone: { seq: number; count: number } | undefined;
for (const b of bag) {
  const p = ingestPacket(b);
  if (!p) continue;
  const items = parseBagItems(p);
  stone = items.find((i) => i.itemId === TELEPORT_STONE_ITEM_ID && i.count >= 1 && i.seq > 0);
  if (stone) break;
}
if (!stone) throw new Error("no stone 800");
console.log("target", expect, "cur", cur, "obj", obj, "exit", exitId, "stoneSeq", stone.seq);

const prep = await exchange(ws, buildMapTeleportPrepareRequest(cur, obj));
dump("16/4", prep);
let conf: { result: number; value: number } | null = null;
for (const b of prep) {
  const p = ingestPacket(b);
  if (!p) continue;
  conf = parseMapTeleportConfirm(p) || conf;
}
if (!conf || conf.result !== 0) throw new Error(`confirm fail ${JSON.stringify(conf)}`);

const combo = await exchange(
  ws,
  buildMapTeleportConfirmedExitRequest(exitId, conf.value || 1, stone.seq, conf.value || 1),
);
dump("combo", combo);

await sleep(150);
let landed = "";
let pollNpcs = 0;
for (let i = 0; i < 40 && !landed; i++) {
  const poll = await exchange(ws, new Uint8Array(0), CBMS_FLAG_SCENE_POLL);
  const d = dump(`poll#${i}`, poll);
  if (d.scene) landed = d.scene;
  if (d.npcs.length) pollNpcs = d.npcs.length;
  if (!landed) await sleep(150);
}
console.log("landed", landed, "pollNpcPeak", pollNpcs);

const complete = dump(
  "2/3-complete",
  await exchange(ws, buildSceneChangeRequest(expect, 0, 2)),
);
const catalog = dump(
  "6/1-catalog",
  await exchange(ws, buildSceneNpcCatalogFollowupRequest()),
);

const ok = catalog.npcs.length > 0 || complete.npcs.length > 0 || pollNpcs > 0;
console.log(
  "RESULT",
  ok ? "HAS_NPC" : "NO_NPC",
  "complete×",
  complete.npcs.length,
  "catalog×",
  catalog.npcs.length,
  "pollPeak×",
  pollNpcs,
);
ws.close();
if (!catalog.npcs.length) process.exit(2);
