import fs from "node:fs";
import { unwrapResource } from "../src/resources/lzss.ts";
import { decodeLkv2Gif } from "../src/resources/lkv2Gif.ts";

const g = decodeLkv2Gif(unwrapResource(fs.readFileSync("../bin/JHOnlineData/tiao.gif")));
console.log("tiao", g.width, g.height);
// Dump ASCII map of non-black pixels by row to find bar interiors
for (let y = 0; y < g.height; y++) {
  let row = "";
  for (let x = 0; x < g.width; x++) {
    const i = (y * g.width + x) * 4;
    const r = g.rgba[i]!, gre = g.rgba[i + 1]!, b = g.rgba[i + 2]!, a = g.rgba[i + 3]!;
    if (a < 128) row += " ";
    else if (r < 40 && gre < 40 && b < 40) row += ".";
    else if (r > 180 && gre > 180 && b > 180) row += "W";
    else if (r > 160 && gre > 120 && b < 80) row += "G";
    else row += "#";
  }
  console.log(String(y).padStart(2), row);
}
