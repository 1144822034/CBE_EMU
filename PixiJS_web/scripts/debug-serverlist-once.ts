/**
 * Dump one WT 1/1/12 login exchange via local gateway.
 * Usage: npx tsx scripts/debug-serverlist-once.ts [user] [pass]
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
  ingestPacket,
  loadClientCodeVersion,
  parseServerList,
} from "../src/net/protocol";

const url = process.env.PIX_GATEWAY || "ws://127.0.0.1:19092";
const user = process.argv[2] ?? "";
const pass = process.argv[3] ?? "";

function asciiPreview(u: Uint8Array, n = 240): string {
  return [...u.slice(0, n)]
    .map((b) => (b >= 32 && b < 127 ? String.fromCharCode(b) : "."))
    .join("");
}

const cv = await loadClientCodeVersion();
const ws = new WebSocket(url);
await new Promise<void>((resolve, reject) => {
  const t = setTimeout(() => reject(new Error(`connect timeout ${url}`)), 2500);
  ws.once("open", () => {
    clearTimeout(t);
    resolve();
  });
  ws.once("error", (err) => {
    clearTimeout(t);
    reject(err);
  });
});

const bodies: Uint8Array[] = [];
await new Promise<void>((resolve, reject) => {
  const t = setTimeout(() => reject(new Error("exchange timeout")), 8000);
  ws.on("message", (data) => {
    const buf =
      data instanceof Buffer ? new Uint8Array(data) : new Uint8Array(data as ArrayBuffer);
    const frame = decodeCbmr(buf);
    console.log(
      JSON.stringify({
        flags: frame.flags,
        eventType: frame.eventType,
        bodyLen: frame.body.length,
      }),
    );
    if (frame.body.length) bodies.push(frame.body);
    if (!(frame.flags & CBMR_FLAG_HAS_FOLLOWUP)) {
      clearTimeout(t);
      resolve();
    }
  });
  const wt = buildLoginAlt12Request(user, pass, cv);
  console.log(
    JSON.stringify({
      url,
      reqLen: wt.length,
      codeVersion: cv,
      user,
      passLen: pass.length,
    }),
  );
  ws.send(
    Buffer.from(encodeCbms(0, encodeRequestMeta((Math.random() * 0xffffff) >>> 0), wt)),
  );
});
ws.close();

for (let i = 0; i < bodies.length; i++) {
  const body = bodies[i]!;
  console.log(`body[${i}] ascii=`, asciiPreview(body));
  const pkt = ingestPacket(body);
  if (!pkt) {
    console.log(`body[${i}] ingest=null`);
    continue;
  }
  console.log(
    `body[${i}] objects=`,
    pkt.objects.map((o) => `${o.kind}/${o.channel}/${o.subtype}`),
  );
  for (const o of pkt.objects) {
    console.log(
      `  obj ${o.kind}/${o.subtype} fields=`,
      o.fields.map((f) => f.name),
    );
  }
  console.log(`body[${i}] parseServerList=`, parseServerList(pkt));
}

if (!bodies.some((b) => parseServerList(ingestPacket(b)!).servers.length)) {
  process.exitCode = 1;
}
