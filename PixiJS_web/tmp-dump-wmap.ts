import { readFileSync, writeFileSync } from "fs";
import { decodeGbk, readU32LE } from "./src/resources/binary.ts";
function parseDsh(path: string) {
  const raw = new Uint8Array(readFileSync(path));
  const cols = readU32LE(raw, 4), rows = readU32LE(raw, 8), headerBytes = readU32LE(raw, 12);
  let pos = 16 + headerBytes; const out: string[][] = [];
  for (let row = 0; row < rows && pos + 4 <= raw.length; row++) {
    const rowLen = readU32LE(raw, pos); let rp = pos + 4; const end = rp + rowLen; const cells: string[] = [];
    for (let c = 0; c < cols && rp < end; c++) {
      const vl = raw[rp++]!;
      cells.push(decodeGbk(raw.subarray(rp, rp + vl)).replace(/\0+$/, "").trim());
      rp += vl;
    }
    out.push(cells); pos = end;
  }
  return out;
}
const w = parseDsh("../bin/JHOnlineData/wMap.dsh");
const s = parseDsh("../bin/JHOnlineData/sMap.dsh");
const keys = ["泰山", "野猪", "临安", "桃花", "蓬莱"];
const result: any = {};
for (const name of keys) {
  result[name] = {
    w: w.filter((c) => (c[2]! + c[3]!).includes(name)).map((c) => ({ id: +c[0]!, tp: +c[1]!, name: c[2], alias: c[3], base: +c[12]!, n: +c[13]! })),
    s: s.filter((c) => (c[1]! + c[2]!).includes(name)).slice(0, 8).map((c) => ({ id: +c[0]!, scene: c[1], alias: c[2], parent: +c[11]! })),
  };
}
writeFileSync("tmp-wmap-dump.json", JSON.stringify(result, null, 2), "utf8");
console.log("wrote tmp-wmap-dump.json");
