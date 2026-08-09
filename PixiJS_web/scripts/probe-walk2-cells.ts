import fs from "node:fs";
import { unwrapResource } from "../src/resources/lzss.ts";
import { decodeLkv2Gif } from "../src/resources/lkv2Gif.ts";

const walk2 = decodeLkv2Gif(
  unwrapResource(fs.readFileSync("../bin/JHOnlineData/hW_warriorwalk2.gif")),
);
console.log("walk2", walk2.width + "x" + walk2.height);

// Dump each 15-wide cell
const cellW = [0, 15, 30, 46];
const cellWe = [15, 15, 16, 16];
for (let ci = 0; ci < 4; ci++) {
  const x0 = cellW[ci]!;
  const w = cellWe[ci]!;
  const h = walk2.height;
  console.log(`\n==== cell${ci} @${x0} ${w}x${h}`);
  const sx = Math.max(1, Math.floor(w / 12));
  const sy = Math.max(1, Math.floor(h / 12));
  for (let y = 0; y < h; y += sy) {
    let row = "";
    for (let x = 0; x < w; x += sx) {
      const i = (y * walk2.width + (x0 + x)) * 4;
      row += walk2.rgba[i + 3]! < 40 ? " " : "#";
    }
    console.log(row);
  }
}
