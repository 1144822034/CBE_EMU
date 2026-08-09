import fs from "node:fs";
import { unwrapResource } from "../src/resources/lzss.ts";
import { decodeLkv2Gif } from "../src/resources/lkv2Gif.ts";

for (const n of [
  "riches_name0.gif",
  "level_name0.gif",
  "level_name5.gif",
  "riches_name9.gif",
]) {
  const g = decodeLkv2Gif(unwrapResource(fs.readFileSync(`../bin/JHOnlineData/${n}`)));
  console.log(n, `${g.width}x${g.height}`);
}
