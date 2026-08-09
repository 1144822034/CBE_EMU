import { readFileSync } from "node:fs";
import { unwrapResource } from "../src/resources/lzss.ts";
import { asciiAt, decodeGbk, readU16LE } from "../src/resources/binary.ts";

const root = "../bin/JHOnlineData";
const sce = "01桃花岛_04.sce";
const raw = unwrapResource(new Uint8Array(readFileSync(`${root}/${sce}`)));
const marker = "n_telestone";
const latin = Buffer.from(raw).toString("latin1");
let idx = 0;
let n = 0;
while ((idx = latin.indexOf(marker, idx)) >= 0 && n < 5) {
  const start = Math.max(0, idx - 40);
  const slice = raw.subarray(start, Math.min(raw.length, idx + 80));
  console.log("--- hit", n, "at", idx);
  // dump u16 around
  for (let i = start; i < idx + 4; i += 2) {
    if (i + 1 >= raw.length) break;
    console.log(`  ${i}: u16=${readU16LE(raw, i)} ascii=${asciiAt(raw, i, Math.min(12, raw.length - i))}`);
  }
  // try find length-prefixed string just before marker
  for (let back = 1; back < 8; back++) {
    const lenPos = idx - 1 - back;
    if (lenPos < 0) continue;
    const len = raw[lenPos]!;
    if (len === marker.length + (back === 1 ? 6 : 0) || len === marker.length) {
      console.log("  possible len@", lenPos, "=", len);
    }
  }
  // show bytes as hex around
  const hex = [...raw.subarray(idx - 16, idx + marker.length + 24)]
    .map((b) => b.toString(16).padStart(2, "0"))
    .join(" ");
  console.log("  hex", hex);
  idx += marker.length;
  n++;
}
console.log("total hits", n);

// also search n_teleporter
const m2 = "n_teleporter";
idx = latin.indexOf(m2);
console.log("teleporter at", idx);
if (idx >= 0) {
  console.log(
    "hex",
    [...raw.subarray(idx - 16, idx + m2.length + 24)]
      .map((b) => b.toString(16).padStart(2, "0"))
      .join(" "),
  );
}
