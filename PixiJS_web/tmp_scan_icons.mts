import fs from "fs";
import path from "path";
import { unwrapResource } from "./src/resources/lzss.ts";
import { decodeLkv2Gif } from "./src/resources/lkv2Gif.ts";

const root = "../bin/JHOnlineData";
const out: Array<[string, number, number, number]> = [];
for (const name of fs.readdirSync(root)) {
  if (!name.toLowerCase().endsWith(".gif")) continue;
  try {
    const g = decodeLkv2Gif(unwrapResource(fs.readFileSync(path.join(root, name))));
    if (g.height === 16 && g.width >= 32 && g.width % 16 === 0) {
      out.push([name, g.width, g.height, g.width / 16]);
    } else if (g.width === 16 && g.height >= 32 && g.height % 16 === 0) {
      out.push([name, g.width, g.height, -(g.height / 16)]);
    } else if (name.toLowerCase().includes("kuang") || name.toLowerCase().includes("icon")) {
      out.push([name, g.width, g.height, 0]);
    }
  } catch {
    /* skip */
  }
}
out.sort((a, b) => a[0].localeCompare(b[0]));
for (const row of out) console.log(row.join("\t"));
