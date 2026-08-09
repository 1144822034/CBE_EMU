import fs from "node:fs";
import { unwrapResource } from "../src/resources/lzss.ts";
import { decodeGbk, readU16LE } from "../src/resources/binary.ts";

function scenePayloadStart(data: Uint8Array): number {
  for (let base = 0; base + 12 <= data.length && base < 32; base++) {
    if (
      data[base] === 0x53 &&
      data[base + 1] === 0x43 &&
      data[base + 2] === 0x45 &&
      data[base + 3] === 0x32
    ) {
      return base;
    }
  }
  return 0;
}

function readSceStringField(
  data: Uint8Array,
  posRef: { pos: number },
  field: number,
): string | null {
  const pos = posRef.pos;
  if (pos + 5 > data.length) return null;
  if (readU16LE(data, pos) !== 3 || readU16LE(data, pos + 2) !== field) return null;
  const n = data[pos + 4]!;
  if (pos + 5 + n > data.length) return null;
  const s = decodeGbk(data.subarray(pos + 5, pos + 5 + n));
  posRef.pos = pos + 5 + n;
  return s;
}

function parseCombatAt(data: Uint8Array, off: number) {
  if (off + 14 > data.length || readU16LE(data, off) !== 3) return null;
  let pos = off + 2;
  const x = readU16LE(data, pos);
  const y = readU16LE(data, pos + 2);
  pos += 4;
  if (!x || !y || pos + 8 > data.length) return null;
  const metaKind = readU16LE(data, pos);
  if (
    (metaKind !== 5 && metaKind !== 6) ||
    readU16LE(data, pos + 4) !== 0x0e
  ) {
    return null;
  }
  const actorId = readU16LE(data, pos + 6);
  pos += 8;
  if (!actorId) return null;
  const posRef = { pos };
  const displayName = readSceStringField(data, posRef, 0x0f);
  if (!displayName) return null;
  pos = posRef.pos;
  if (pos + 6 > data.length || readU16LE(data, pos) !== 1) return null;
  const field = readU16LE(data, pos + 2);
  const visualHint = readU16LE(data, pos + 4);
  pos += 6;
  if (field !== 0x10) return null;
  posRef.pos = pos;
  const actorResource = readSceStringField(data, posRef, 0x11);
  if (!actorResource?.endsWith(".actor")) return null;
  return { actorId, x, y, displayName, actorResource, visualHint, end: posRef.pos };
}

const scenes = [
  "01桃花岛_01.sce",
  "01桃花岛_02.sce",
  "c00蓬莱仙岛_01.sce",
  "11终南山_01.sce",
];
for (const scene of scenes) {
  const p = `../bin/JHOnlineData/${scene}`;
  if (!fs.existsSync(p)) {
    console.log(scene, "MISSING");
    continue;
  }
  const data = unwrapResource(fs.readFileSync(p));
  const start = scenePayloadStart(data);
  const list = [];
  for (let off = start; off + 14 <= data.length; off++) {
    const s = parseCombatAt(data, off);
    if (!s) continue;
    list.push(s);
    if (s.end > off) off = s.end - 1;
  }
  console.log(scene, "combat", list.length);
  for (const s of list.slice(0, 8)) {
    console.log(
      " ",
      s.actorId,
      s.x,
      s.y,
      s.displayName,
      s.actorResource,
    );
  }
}
