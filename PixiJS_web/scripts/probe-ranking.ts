/**
 * Login → enter first role → dump 23/7 scores for type 0/1/2.
 * Usage: npx tsx scripts/probe-ranking.ts [user] [pass]
 */
import WebSocket from "ws";
import {
  CBMR_FLAG_HAS_FOLLOWUP,
  decodeCbmr,
  encodeCbms,
  encodeRequestMeta,
} from "../src/net/cbms";
import {
  buildLoginAlt12Request,
  buildRoleSelectRequest,
  buildServerSelectRequest,
  ingestPacket,
  loadClientCodeVersion,
  parseRoleListFromSelect,
  parseServerList,
} from "../src/net/protocol";
import {
  buildRankingBoardRequest,
  parseRankingBoard,
} from "../src/net/protocolExtras";
import { findField, SeqReader } from "../src/net/wt";

function hex(u: Uint8Array, n = 48): string {
  return [...u.slice(0, n)].map((b) => b.toString(16).padStart(2, "0")).join(" ");
}

const url = process.env.PIX_GATEWAY || "ws://192.1.1.2:19092";
const user = process.argv[2] || process.env.PIX_USER || "lxh001";
const pass = process.argv[3] || process.env.PIX_PASS || "123456";
const clientId = ((Math.random() * 0xffffff) >>> 0) || 1;

function exchange(
  ws: WebSocket,
  wt: Uint8Array,
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
        data instanceof Buffer
          ? new Uint8Array(data)
          : new Uint8Array(data as ArrayBuffer);
      try {
        const frame = decodeCbmr(buf);
        if (frame.body.length) bodies.push(frame.body);
        if ((frame.flags & CBMR_FLAG_HAS_FOLLOWUP) === 0) {
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
  const t = setTimeout(() => reject(new Error("connect timeout")), 3000);
  ws.once("open", () => {
    clearTimeout(t);
    resolve();
  });
  ws.once("error", reject);
});

const cv = await loadClientCodeVersion();
console.log({ url, user, codeVersion: cv });

let bodies = await exchange(ws, buildLoginAlt12Request(user, pass, cv));
let servers: ReturnType<typeof parseServerList>["servers"] = [];
for (const b of bodies) {
  const p = ingestPacket(b);
  if (!p) continue;
  const list = parseServerList(p);
  if (list.servers.length) servers = list.servers;
}
if (!servers.length) throw new Error("no servers");

bodies = await exchange(ws, buildServerSelectRequest(servers[0]!.serverId));
let roles: ReturnType<typeof parseRoleListFromSelect> = [];
for (const b of bodies) {
  const p = ingestPacket(b);
  if (!p) continue;
  const r = parseRoleListFromSelect(p);
  if (r.length) roles = r;
}
if (!roles.length) throw new Error("no roles");
console.log(
  "roles",
  roles.slice(0, 5).map((r) => `${r.roleId}:${r.name}`),
);

bodies = await exchange(ws, buildRoleSelectRequest(roles[0]!.roleId));
console.log("enter bodies", bodies.length);

for (const type of [0, 1, 2]) {
  bodies = await exchange(ws, buildRankingBoardRequest(type, 0));
  let page = null;
  for (const b of bodies) {
    const p = ingestPacket(b);
    if (!p) continue;
    const obj = p.objects.find((o) => o.kind === 23 && o.subtype === 7);
    if (obj) {
      console.log(
        `typeReq=${type} fields=${obj.fields.map((f) => f.name).join(",")}`,
      );
      if (type === 2) {
        const top = findField(obj, "topplayerinfo");
        if (top) {
          console.log(`top len=${top.length} head=${hex(top, 96)}`);
          const r = new SeqReader(top);
          for (let i = 0; i < 10 && r.remaining() > 0; i++) {
            const at = r.pos;
            try {
              const roleId = r.u32();
              const nameAt = r.pos;
              const name = r.string().replace(/\0+$/g, "");
              const scoreAt = r.pos;
              const score = r.string().replace(/\0+$/g, "");
              console.log(
                `row${i} @${at} id=${roleId} name=${JSON.stringify(name)} score=${JSON.stringify(score)} nameHex=${hex(top.subarray(nameAt, scoreAt))} scoreHex=${hex(top.subarray(scoreAt, r.pos))}`,
              );
            } catch (e) {
              console.log(`row${i} @${at} FAIL`, e);
              break;
            }
          }
        }
      }
    }
    const parsed = parseRankingBoard(p, type);
    if (parsed) page = parsed;
  }
  if (!page) {
    console.log(`typeReq=${type} NO_PAGE bodies=${bodies.length}`);
    continue;
  }
  const focus = page.rows.filter((r) => [1, 2, 4, 8, 297].includes(r.roleId));
  console.log(
    JSON.stringify({
      typeReq: type,
      pageType: page.type,
      myorder: page.myorder,
      rowCount: page.rows.length,
      focus,
      first8: page.rows.slice(0, 8).map((r) => ({
        id: r.roleId,
        score: r.score,
        name: r.name,
      })),
      nonzero: page.rows
        .filter((r) => r.score !== "0")
        .slice(0, 10)
        .map((r) => ({ id: r.roleId, score: r.score })),
    }),
  );
}

ws.close();
