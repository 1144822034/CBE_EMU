import fs from "node:fs";
import { unwrapResource } from "../src/resources/lzss.ts";
import { decodeLkv2Gif } from "../src/resources/lkv2Gif.ts";

function dump(name: string) {
  const g = decodeLkv2Gif(unwrapResource(fs.readFileSync(`../bin/JHOnlineData/${name}`)));
  console.log(name, g.width + "x" + g.height);
  const stepX = Math.max(1, Math.floor(g.width / 16));
  const stepY = Math.max(1, Math.floor(g.height / 12));
  for (let y = 0; y < g.height; y += stepY) {
    let row = "";
    for (let x = 0; x < g.width; x += stepX) {
      const i = (y * g.width + x) * 4;
      const r = g.rgba[i]!, gg = g.rgba[i + 1]!, b = g.rgba[i + 2]!, a = g.rgba[i + 3]!;
      if (a < 128) row += " ";
      else if (r > 180 && gg < 90 && b < 90) row += "R";
      else if (r > 180 && gg > 140 && b < 100) row += "Y";
      else if (b > 140 && r < 100) row += "B";
      else if (gg > 140 && r < 100) row += "G";
      else if (r > 160 && gg > 120 && b < 100) row += "O";
      else row += ".";
    }
    console.log(row);
  }
}

for (const n of [
  "UIsh1.gif",
  "UIsh2.gif",
  "UIsh5.gif",
  "UIsh6.gif",
  "UIRed.gif",
  "training.gif",
]) {
  try {
    dump(n);
  } catch (e) {
    console.log(n, String(e));
  }
}
