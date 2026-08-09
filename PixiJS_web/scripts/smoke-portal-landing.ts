/**
 * Live portal transfer: dump 2/3 + post-enter objects and parsed landing.
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
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
  buildSceneChangePostEnterFollowupRequest,
  ingestPacket,
  parseRoleListFromSelect,
  parseRoleSelectEnter,
  parseSceneEnter,
  parseServerList,
} from "../src/net/protocol";
import {
  buildSceneChangePreludeRequest,
  buildSceneChangeRequest,
  parseSceneReload,
  parseServerSceneLanding,
  parseNamedSceneLanding,
} from "../src/net/protocolExtras";
import { SeqReader, findField, decodeFieldString } from "../src/net/wt";
import { loadSceneMap } from "../src/resources/loader";

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const assetRoot = [
  path.join(repo, "web", "fs", "JHOnlineData"),
  path.join(repo, "bin", "JHOnlineData"),
].find((p) => fs.existsSync(p));
if (!assetRoot) throw new Error("JHOnlineData not found");
globalThis.fetch = (async (input: RequestInfo | URL) => {
  const url = String(input);
  const marker = "/JHOnlineData/";
  const idx = url.indexOf(marker);
  const name = decodeURIComponent(url.slice(idx + marker.length).split("?")[0]!);
  const buf = fs.readFileSync(path.join(assetRoot!, name));
  return {
    ok: true,
    status: 200,
    arrayBuffer: async () =>
      buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength),
  } as Response;
}) as typeof fetch;

const url = process.env.PIX_GATEWAY || "ws://127.0.0.1:19092";
const user = process.env.PIX_USER || "lxh001";
const pass = process.env.PIX_PASS || "123456";
const clientId = (Math.random() * 0xffffff) >>> 0;

function exchange(ws: WebSocket, wt: Uint8Array, timeoutMs = 12000): Promise<Uint8Array[]> {
  return new Promise((resolveP, reject) => {
    const bodies: Uint8Array[] = [];
    let pending = false;
    const timer = setTimeout(() => {
      cleanup();
      if (bodies.length) resolveP(bodies);
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
          resolveP(bodies);
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

function dumpLanding(label: string, bodies: Uint8Array[]) {
  console.log(`\n=== ${label} bodies=${bodies.length} ===`);
  for (const [i, b] of bodies.entries()) {
    const p = ingestPacket(b);
    if (!p) {
      console.log(i, "not wt", b.length);
      continue;
    }
    console.log(i, p.objects.map((o) => `${o.kind}/${o.subtype}`).join(", "));
    for (const o of p.objects) {
      if (o.kind === 0x1e || (o.kind === 0x1b && o.subtype === 12) || o.kind === 0x10) {
        const scene = findField(o, "scene") || findField(o, "name");
        const pos = findField(o, "posinfo");
        let xy = "-";
        if (pos) {
          try {
            const r = new SeqReader(pos);
            xy = `${r.i16()},${r.i16()} (len=${pos.length} hex=${Buffer.from(pos).toString("hex")})`;
          } catch (e) {
            xy = `parse-fail len=${pos.length} hex=${Buffer.from(pos).toString("hex")} err=${e}`;
          }
        }
        console.log(
          `  obj ${o.kind}/${o.subtype} scene=${scene ? decodeFieldString(scene) : "-"} pos=${xy}`,
        );
      }
    }
    const land = parseServerSceneLanding(p);
    const reload = parseSceneReload(p);
    const named = parseNamedSceneLanding(p);
    if (land) console.log("  LANDING", land);
    if (reload) console.log("  RELOAD", reload);
    if (named) console.log("  NAMED", named);
  }
}

const ws = new WebSocket(url);
await new Promise<void>((res, rej) => {
  const t = setTimeout(() => rej(new Error("conn")), 2500);
  ws.once("open", () => {
    clearTimeout(t);
    res();
  });
  ws.once("error", rej);
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

let mapId = process.env.PIX_MAP || "";
let exitId = Number(process.env.PIX_EXIT || "0");
if (!mapId) {
  const loaded = await loadSceneMap(enter.scene);
  const portals = loaded.portals ?? [];
  console.log(
    "portals",
    portals.length,
    portals.slice(0, 4).map((p) => ({
      kind: p.kind,
      entryId: p.entryId,
      targetEntryId: p.targetEntryId,
      target: p.targetScene,
      spawn: [p.spawnX, p.spawnY],
    })),
  );
  const p0 = portals.find((p) => p.kind === "edge" || p.kind === "named") ?? portals[0];
  if (!p0) throw new Error("no portals in " + enter.scene);
  mapId = p0.targetScene;
  // exitID = source entryId (0x07); landing = dest portal with same entryId.
  exitId = p0.entryId;
  console.log("using portal", {
    mapId,
    exitId,
    kind: p0.kind,
    aabbSpawn: [p0.spawnX, p0.spawnY],
    entryId: p0.entryId,
    targetEntry: p0.targetEntryId,
  });
}

const pre = await exchange(ws, buildSceneChangePreludeRequest());
dumpLanding("prelude 2/10", pre);
const change = await exchange(ws, buildSceneChangeRequest(mapId, exitId, 2));
dumpLanding("change 2/3", change);
const post = await exchange(ws, buildSceneChangePostEnterFollowupRequest(mapId, exitId, 2));
dumpLanding("post-enter", post);

ws.close();
console.log("\nDONE");
