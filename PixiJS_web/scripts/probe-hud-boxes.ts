import fs from "node:fs";
import { unwrapResource } from "../src/resources/lzss.ts";
import { decodeLkv2Gif } from "../src/resources/lkv2Gif.ts";

const g = decodeLkv2Gif(unwrapResource(fs.readFileSync("../bin/JHOnlineData/tiao.gif")));
// Find horizontal runs of near-black with a>200, length>=20
type Run = { y: number; x0: number; x1: number };
const runs: Run[] = [];
for (let y = 0; y < g.height; y++) {
  let x = 0;
  while (x < g.width) {
    const i = (y * g.width + x) * 4;
    const r = g.rgba[i]!,
      gre = g.rgba[i + 1]!,
      b = g.rgba[i + 2]!,
      a = g.rgba[i + 3]!;
    const black = a > 200 && r < 30 && gre < 30 && b < 30;
    if (!black) {
      x++;
      continue;
    }
    const x0 = x;
    while (x < g.width) {
      const j = (y * g.width + x) * 4;
      const rr = g.rgba[j]!,
        gg = g.rgba[j + 1]!,
        bb = g.rgba[j + 2]!,
        aa = g.rgba[j + 3]!;
      if (!(aa > 200 && rr < 30 && gg < 30 && bb < 30)) break;
      x++;
    }
    if (x - x0 >= 20) runs.push({ y, x0, x1: x - 1 });
  }
}
console.log("long black runs:");
for (const r of runs) console.log(r);

// handkuang transparent interior
{
  const h = decodeLkv2Gif(unwrapResource(fs.readFileSync("../bin/JHOnlineData/handkuang.gif")));
  let minX = 999,
    minY = 999,
    maxX = -1,
    maxY = -1,
    n = 0;
  for (let y = 0; y < h.height; y++) {
    for (let x = 0; x < h.width; x++) {
      const i = (y * h.width + x) * 4;
      // blueish interior or near-black
      const r = h.rgba[i]!,
        gre = h.rgba[i + 1]!,
        b = h.rgba[i + 2]!,
        a = h.rgba[i + 3]!;
      if (a > 200 && b > r + 20 && b > gre) {
        n++;
        if (x < minX) minX = x;
        if (y < minY) minY = y;
        if (x > maxX) maxX = x;
        if (y > maxY) maxY = y;
      }
    }
  }
  console.log("handkuang blue interior", minX, minY, maxX, maxY, "n", n, "size", h.width, h.height);
}

for (const name of ["uiexp.gif", "bag.gif", "UI7.gif", "ui_hpspnum.gif", "numberexp.gif", "handkuang.gif"]) {
  const x = decodeLkv2Gif(unwrapResource(fs.readFileSync(`../bin/JHOnlineData/${name}`)));
  console.log(name, x.width + "x" + x.height);
}
