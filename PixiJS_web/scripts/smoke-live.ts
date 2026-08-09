/**
 * Live gateway smoke: 1/12 server list via ws://127.0.0.1:19092 → :19090.
 * Requires: jh-online-server + `npm run gateway`.
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
  parseServerList,
} from "../src/net/protocol";

const url = process.env.PIX_GATEWAY || "ws://127.0.0.1:19092";
const clientId = (Math.random() * 0xffffff) >>> 0;

function exchange(ws: WebSocket, wt: Uint8Array, timeoutMs = 5000): Promise<Uint8Array[]> {
  return new Promise((resolve, reject) => {
    const bodies: Uint8Array[] = [];
    const timer = setTimeout(() => {
      cleanup();
      reject(new Error("exchange timeout"));
    }, timeoutMs);
    const onMsg = (data: WebSocket.RawData) => {
      const buf = data instanceof Buffer ? new Uint8Array(data) : new Uint8Array(data as ArrayBuffer);
      try {
        const frame = decodeCbmr(buf);
        if (frame.body.length) bodies.push(frame.body);
        if (!(frame.flags & CBMR_FLAG_HAS_FOLLOWUP)) {
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

const bodies = await exchange(ws, buildLoginAlt12Request());
ws.close();

let servers = 0;
let name = "";
for (const body of bodies) {
  const pkt = ingestPacket(body);
  if (!pkt) continue;
  const list = parseServerList(pkt);
  if (list.servers.length) {
    servers = list.servers.length;
    name = list.servers[0]!.name;
    break;
  }
}

if (!servers) {
  console.error("live smoke FAIL: no servers in", bodies.length, "bodies");
  process.exit(1);
}
console.log("live smoke OK", { url, bodies: bodies.length, servers, first: name });
