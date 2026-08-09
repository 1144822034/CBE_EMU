import { readFileSync, writeFileSync } from "fs";
import { decodeGbk, readU32LE } from "../src/resources/binary.ts";

function parseDsh(path: string): string[][] {
  const raw = new Uint8Array(readFileSync(path));
  const cols = readU32LE(raw, 4);
  const rows = readU32LE(raw, 8);
  const headerBytes = readU32LE(raw, 12);
  let pos = 16 + headerBytes;
  const out: string[][] = [];
  for (let row = 0; row < rows && pos + 4 <= raw.length; row++) {
    const rowLen = readU32LE(raw, pos);
    let rp = pos + 4;
    const end = rp + rowLen;
    const cells: string[] = [];
    for (let c = 0; c < cols && rp < end; c++) {
      const vl = raw[rp++]!;
      cells.push(decodeGbk(raw.subarray(rp, rp + vl)).replace(/\0+$/, "").trim());
      rp += vl;
    }
    out.push(cells);
    pos = end;
  }
  return out;
}

const w = parseDsh("../bin/JHOnlineData/wMap.dsh");
const s = parseDsh("../bin/JHOnlineData/sMap.dsh");
const lines: string[] = [];
for (const c of w) {
  lines.push(`W id=${c[0]} tp=${c[1]} name=${c[2]}/${c[3]} base=${c[12]} n=${c[13]}`);
}
const tai = w.find((c) => (c[3] || "").includes("泰山") || (c[2] || "").includes("泰山"));
lines.push("--- taishan children ---");
if (tai) {
  for (const c of s.filter((x) => x[11] === tai[0])) {
    lines.push(`S id=${c[0]} file=${c[1]} alias=${c[2]} parent=${c[11]}`);
  }
}
lines.push("--- tiandu ---");
for (const c of s.filter((x) => (x[2] || "").includes("天都"))) {
  lines.push(`S id=${c[0]} file=${c[1]} alias=${c[2]} parent=${c[11]}`);
}
writeFileSync("tmp-tiandu.txt", lines.join("\n"), "utf8");
console.log("wrote", lines.length);
