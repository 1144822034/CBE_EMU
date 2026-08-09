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

const url = process.env.PIX_GATEWAY || "ws://47.108.210.191:19092";
const ver = Number(process.env.PIX_CODE_VERSION || "2");
const clientId = (Math.random() * 0xffffff) >>> 0;

console.log("probe", url, "codeVersion", ver);

const ws = new WebSocket(url);
await new Promise<void>((resolve, reject) => {
  const t = setTimeout(() => reject(new Error("open timeout")), 8000);
  ws.once("open", () => {
    clearTimeout(t);
    console.log("WS OPEN");
    resolve();
  });
  ws.once("error", (e) => {
    clearTimeout(t);
    reject(e);
  });
});

const bodies = await new Promise<Uint8Array[]>((resolve, reject) => {
  const out: Uint8Array[] = [];
  const timer = setTimeout(() => {
    cleanup();
    reject(new Error("exchange timeout (gateway OK but mock :19090 may be down)"));
  }, 8000);
  const onMsg = (data: WebSocket.RawData) => {
    const buf =
      data instanceof Buffer
        ? new Uint8Array(data)
        : new Uint8Array(data as ArrayBuffer);
    const frame = decodeCbmr(buf);
    console.log("CBMR flags=", frame.flags, "bodyLen=", frame.body.length);
    if (frame.body.length) out.push(frame.body);
    if (!(frame.flags & CBMR_FLAG_HAS_FOLLOWUP)) {
      cleanup();
      resolve(out);
    }
  };
  const cleanup = () => {
    clearTimeout(timer);
    ws.off("message", onMsg);
  };
  ws.on("message", onMsg);
  ws.send(
    Buffer.from(
      encodeCbms(
        0,
        encodeRequestMeta(clientId),
        buildLoginAlt12Request("", "", ver),
      ),
    ),
  );
});

for (const body of bodies) {
  const pkt = ingestPacket(body);
  if (!pkt) {
    console.log("ingest fail len=", body.length);
    continue;
  }
  const list = parseServerList(pkt);
  console.log(
    "login",
    JSON.stringify({
      result: list.result,
      information: list.information,
      servers: list.servers.length,
      names: list.servers.slice(0, 3).map((s) => s.name),
    }),
  );
  if (list.servers.length > 0) process.exit(0);
  if (list.result === 2) process.exit(2);
}
ws.close();
process.exit(1);
