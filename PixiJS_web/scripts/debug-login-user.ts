import WebSocket from "ws";
import { decodeCbmr, encodeCbms, encodeRequestMeta } from "../src/net/cbms";
import { buildLoginAlt12Request, ingestPacket, parseServerList } from "../src/net/protocol";
import { findField, decodeFieldString, decodeAsciiDigitResult } from "../src/net/wt";

const url = "ws://127.0.0.1:19092";
const user = process.env.PIX_USER || "lxh001";
const pass = process.env.PIX_PASS || "123456";
const clientId = (Math.random() * 0xffffff) >>> 0;
const ws = new WebSocket(url);
await new Promise<void>((res, rej) => {
  ws.once("open", () => res());
  ws.once("error", rej);
});
const wt = buildLoginAlt12Request(user, pass);
console.log("login", user, pass, "wt", wt.length);
const bodies: Uint8Array[] = await new Promise((resolve, reject) => {
  const t = setTimeout(() => reject(new Error("timeout")), 5000);
  const out: Uint8Array[] = [];
  ws.on("message", (data) => {
    const f = decodeCbmr(new Uint8Array(data as Buffer));
    console.log("cbmr body", f.body.length);
    if (f.body.length) out.push(f.body);
    clearTimeout(t);
    resolve(out);
  });
  ws.send(Buffer.from(encodeCbms(0, encodeRequestMeta(clientId), wt)));
});
for (const b of bodies) {
  const p = ingestPacket(b)!;
  for (const o of p.objects) {
    console.log("obj", o.kind, o.subtype, o.fields.map((f) => f.name).join(","));
    const r = findField(o, "result");
    console.log(" result", r && [...r], decodeAsciiDigitResult(r ?? new Uint8Array()));
    const info = findField(o, "information");
    if (info) console.log(" info", decodeFieldString(info));
  }
  console.log(parseServerList(p));
}
ws.close();
