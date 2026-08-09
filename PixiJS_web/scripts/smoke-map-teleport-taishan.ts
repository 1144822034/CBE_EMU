/**
 * Live map-stone: 泰山(exit 67) vs control (野猪林 45 if present).
 * Guest/lxh001 — needs stones. Prints confirm/combo/poll scene.
 */
import WebSocket from "ws";
import { writeFileSync } from "fs";
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
  buildSceneStartupFollowupRequest,
  buildServerSelectRequest,
  ensureSceneFile,
  ingestPacket,
  parseRoleListFromSelect,
  parseRoleSelectEnter,
  parseSceneEnter,
  parseServerList,
} from "../src/net/protocol.ts";
import {
  buildBagOpenRequest,
  buildMapTeleportPrepareRequest,
  buildMapTeleportConfirmedExitRequest,
  parseMapTeleportConfirm,
  parseBagItems,
  TELEPORT_STONE_ITEM_ID,
} from "../src/net/protocolExtras.ts";

const url = process.env.PIX_GATEWAY || "ws://127.0.0.1:19092";
const user = process.env.PIX_USER || "lxh001";
const pass = process.env.PIX_PASS || "123456";
const clientId = (Math.random() * 0xffffff) >>> 0;

function sleep(ms: number) {
  return new Promise<void>((r) => setTimeout(r, ms));
}

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
      if (bodies.length) resolve(bodies);
      else reject(new Error("exchange timeout"));
    }, timeoutMs);
    const onMsg = (data: WebSocket.RawData) => {
      const buf =
        data instanceof Buffer ? new Uint8Array(data) : new Uint8Array(data as ArrayBuffer);
      try {
        const frame = decodeCbmr(buf);
        if (frame.body.length) bodies.push(frame.body);
        const pending = (frame.flags & CBMR_FLAG_HAS_FOLLOWUP) !== 0;
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
    ws.send(Buffer.from(encodeCbms(flags, encodeRequestMeta(clientId), wt)));
  });
}

function kinds(bodies: Uint8Array[]): string {
  const out: string[] = [];
  for (const b of bodies) {
    const p = ingestPacket(b);
    if (!p) {
      out.push(`raw${b.length}`);
      continue;
    }
    for (const o of p.objects) out.push(`${o.kind}/${o.subtype}`);
  }
  return out.join(",") || "(empty)";
}

function sceneFrom(bodies: Uint8Array[]): string | null {
  for (const b of bodies) {
    const p = ingestPacket(b);
    if (!p) continue;
    const e = parseSceneEnter(p);
    if (e) return e.scene;
  }
  return null;
}

async function tryTeleport(
  ws: WebSocket,
  label: string,
  cur: number,
  obj: number,
  exitId: number,
  expect: string,
  stoneSeq: number,
) {
  console.log(`\n==== ${label} cur=${cur} obj=${obj} exit=${exitId} expect=${expect} ====`);
  const prep = await exchange(ws, buildMapTeleportPrepareRequest(cur, obj));
  console.log("16/4 kinds", kinds(prep));
  let conf: { result: number; value: number } | null = null;
  for (const b of prep) {
    const p = ingestPacket(b);
    if (!p) continue;
    conf = parseMapTeleportConfirm(p) || conf;
  }
  console.log("confirm", conf);
  if (!conf || conf.result !== 0) return { ok: false, detail: "no-confirm", stoneSeq };

  const combo = await exchange(
    ws,
    buildMapTeleportConfirmedExitRequest(exitId, conf.value || 1, stoneSeq, conf.value || 1),
  );
  console.log("combo kinds", kinds(combo));
  let sawItem = false;
  for (const b of combo) {
    const p = ingestPacket(b);
    if (!p) continue;
    for (const o of p.objects) {
      if (o.kind === 7 && (o.subtype === 1 || o.subtype === 7 || o.subtype === 11)) sawItem = true;
    }
  }
  if (!sawItem) return { ok: false, detail: "no-item-ack", stoneSeq };

  await sleep(150);
  let landed: string | null = null;
  for (let i = 0; i < 40 && !landed; i++) {
    const poll = await exchange(ws, new Uint8Array(0), CBMS_FLAG_SCENE_POLL);
    const k = kinds(poll);
    const sc = sceneFrom(poll);
    if (sc || k !== "(empty)") console.log(`poll#${i}`, k, sc || "-");
    if (sc) landed = sc;
    else await sleep(150);
  }
  const match =
    !!landed &&
    (landed.toLowerCase() === ensureSceneFile(expect).toLowerCase() ||
      landed.includes("泰山") === expect.includes("泰山"));
  console.log("landed", landed, "matchApprox", match);
  return { ok: !!landed, detail: landed || "timeout", stoneSeq };
}

const ws = new WebSocket(url);
await new Promise<void>((res, rej) => {
  ws.once("open", () => res());
  ws.once("error", rej);
});

const login = await exchange(ws, buildLoginAlt12Request(user, pass));
const servers = login.map((b) => ingestPacket(b)).flatMap((p) => (p ? parseServerList(p) : []));
if (!servers.length) throw new Error("no servers");
const sel = await exchange(ws, buildServerSelectRequest(servers[0]!.id));
const roles = sel.flatMap((b) => {
  const p = ingestPacket(b);
  return p ? parseRoleListFromSelect(p) : [];
});
if (!roles.length) throw new Error("no roles");
const roleBodies = await exchange(ws, buildRoleSelectRequest(roles[0]!.index));
let enter = roleBodies.map((b) => ingestPacket(b)).map((p) => (p ? parseRoleSelectEnter(p) : null)).find(Boolean);
if (!enter) {
  for (const b of roleBodies) {
    const p = ingestPacket(b);
    if (!p) continue;
    const e = parseSceneEnter(p);
    if (e) enter = { ...e, hp: 0, hpMax: 0, mp: 0, mpMax: 0, name: "", level: 0, title: "", titleGif: "" };
  }
}
console.log("enter", enter?.scene, enter && `${enter.x},${enter.y}`);
await exchange(ws, buildSceneStartupFollowupRequest());
await exchange(ws, buildGroupType1Request());

const bagBodies = await exchange(ws, buildBagOpenRequest());
const items = bagBodies.flatMap((b) => {
  const p = ingestPacket(b);
  return p ? parseBagItems(p) : [];
});
const stone = items.find((i) => i.itemId === TELEPORT_STONE_ITEM_ID && i.count >= 1 && i.seq > 0);
console.log(
  "stone",
  stone,
  "bag800",
  items.filter((i) => i.itemId === 800).map((i) => ({ seq: i.seq, n: i.count })),
);
if (!stone) throw new Error("no teleport stone");

// wMap: 临安 teleportId typically 4; 泰山 7; 野猪林 parent varies.
// From prior dump: 泰山 tp=7 base=66 n=4; 天都峰 exit=67 scene=07泰山_02.sce
const curFrom = Number(process.env.PIX_CUR || "4");
const r1 = await tryTeleport(ws, "泰山天都峰", curFrom, 7, 67, "07泰山_02.sce", stone.seq);
const r2 = await tryTeleport(
  ws,
  "野猪林对照",
  7,
  Number(process.env.PIX_CTRL_OBJ || "5"),
  Number(process.env.PIX_CTRL_EXIT || "45"),
  process.env.PIX_CTRL_SCENE || "05野猪林_01.sce",
  stone.seq,
);

const summary = { enter: enter?.scene, r1, r2, clientId };
console.log("\nSUMMARY", JSON.stringify(summary, null, 2));
writeFileSync("tmp-teleport-taishan.json", JSON.stringify(summary, null, 2));
ws.close();
