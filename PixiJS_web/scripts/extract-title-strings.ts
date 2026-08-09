import fs from "fs";
import path from "path";
import { execSync } from "child_process";

// Use PowerShell to decode GBK slices: dump candidate offsets where high bytes look like GBK
const cbm = fs.readFileSync("../bin/JHOnlineData/mmTitleMstarWqvga.cbm");
const candidates: { off: number; len: number }[] = [];
let i = 0;
while (i < cbm.length - 1) {
  // GBK lead 0x81-0xFE, trail 0x40-0xFE except 0x7F
  const a = cbm[i]!, b = cbm[i + 1]!;
  if (a >= 0x81 && a <= 0xfe && b >= 0x40 && b <= 0xfe && b !== 0x7f) {
    let j = i;
    while (
      j < cbm.length - 1 &&
      cbm[j]! >= 0x81 &&
      cbm[j]! <= 0xfe &&
      cbm[j + 1]! >= 0x40 &&
      cbm[j + 1]! <= 0xfe &&
      cbm[j + 1]! !== 0x7f
    ) {
      j += 2;
    }
    // also allow ascii mixed?
    if (j - i >= 8) candidates.push({ off: i, len: j - i });
    i = Math.max(j, i + 1);
  } else i++;
}
console.log("gbk runs", candidates.length);
// write raw slices for powershell decode
const outDir = "tmp-gbk-slices";
fs.mkdirSync(outDir, { recursive: true });
const keep = candidates.filter((c) => c.len >= 12 && c.len <= 120).slice(0, 200);
fs.writeFileSync(path.join(outDir, "index.json"), JSON.stringify(keep));
keep.forEach((c, n) => {
  fs.writeFileSync(path.join(outDir, `${n}.bin`), cbm.subarray(c.off, c.off + c.len));
});
console.log("wrote", keep.length);
