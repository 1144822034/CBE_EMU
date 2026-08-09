import fs from "node:fs";
import path from "node:path";
import { unwrapResource } from "../src/resources/lzss.ts";
import { decodeGbk, readU16LE, asciiAt } from "../src/resources/binary.ts";

const root = path.resolve("../bin/JHOnlineData");
const all = fs.readdirSync(root);
const sce17 = all.filter((n) => n.endsWith(".sce") && n.startsWith("17"));
console.log("17*.sce count", sce17.length);
for (const f of sce17) console.log(" ", f);

function readSceLenString(data: Uint8Array, posRef: { pos: number }): string | null {
  const pos = posRef.pos;
  if (pos >= data.length) return null;
  const n = data[pos]!;
  if (pos + 1 + n > data.length) return null;
  const s = decodeGbk(data.subarray(pos + 1, pos + 1 + n));
  posRef.pos = pos + 1 + n;
  return s;
}

function readSceStringField(
  data: Uint8Array,
  posRef: { pos: number },
  field: number,
): string | null {
  const pos = posRef.pos;
  if (pos + 5 > data.length) return null;
  if (readU16LE(data, pos) !== 3 || readU16LE(data, pos + 2) !== field) return null;
  posRef.pos = pos + 4;
  return readSceLenString(data, posRef);
}

function parseCombat(data: Uint8Array, off: number) {
  if (off + 14 > data.length || readU16LE(data, off) !== 3) return null;
  let pos = off + 2;
  const x = readU16LE(data, pos);
  const y = readU16LE(data, pos + 2);
  pos += 4;
  if (!x || !y || pos + 8 > data.length) return null;
  const metaKind = readU16LE(data, pos);
  if ((metaKind !== 5 && metaKind !== 6) || readU16LE(data, pos + 4) !== 0x0e) return null;
  const actorId = readU16LE(data, pos + 6);
  pos += 8;
  if (!actorId) return null;
  const posRef = { pos };
  const name = readSceStringField(data, posRef, 0x0f);
  if (!name) return null;
  pos = posRef.pos;
  if (pos + 6 > data.length || readU16LE(data, pos) !== 1) return null;
  const field = readU16LE(data, pos + 2);
  const visualHint = readU16LE(data, pos + 4);
  pos += 6;
  if (field !== 0x10) return null;
  posRef.pos = pos;
  const actorResource = readSceStringField(data, posRef, 0x11);
  if (!actorResource?.toLowerCase().endsWith(".actor")) return null;
  let actorResource2 = "";
  pos = posRef.pos;
  if (pos + 5 <= data.length && readU16LE(data, pos) === 3 && readU16LE(data, pos + 2) === 3) {
    const trail = { pos };
    const s2 = readSceStringField(data, trail, 3);
    if (s2?.toLowerCase().endsWith(".actor")) {
      actorResource2 = s2;
      pos = trail.pos;
    }
  }
  return { actorId, x, y, name, actorResource, actorResource2, visualHint, end: pos };
}

for (const scene of sce17) {
  const data = unwrapResource(fs.readFileSync(path.join(root, scene)));
  let start = 0;
  for (let i = 0; i < 32 && i + 4 <= data.length; i++) {
    if (asciiAt(data, i, 4) === "SCE2") {
      start = i;
      break;
    }
  }
  const list = [];
  for (let off = start; off + 14 <= data.length; off++) {
    const s = parseCombat(data, off);
    if (!s) continue;
    list.push(s);
    if (s.end > off) off = s.end - 1;
  }
  console.log("\n====", scene, "combat", list.length);
  for (const s of list.slice(0, 8)) {
    console.log(
      `  id=${s.actorId} (${s.x},${s.y}) "${s.name}"`,
      `f17=${s.actorResource}`,
      `f3=${s.actorResource2 || "-"}`,
    );
  }
}
