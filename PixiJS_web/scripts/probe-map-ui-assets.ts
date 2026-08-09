import { readFileSync, writeFileSync, mkdirSync } from "fs";
import { unwrapResource } from "../src/resources/lzss.ts";
import { decodeLkv2Gif } from "../src/resources/lkv2Gif.ts";

mkdirSync("tmp-map-ui", { recursive: true });

function writePpm(path: string, w: number, h: number, rgba: Uint8ClampedArray) {
  const rows: string[] = [`P3\n${w} ${h}\n255`];
  for (let i = 0; i < w * h; i++) {
    const o = i * 4;
    rows.push(`${rgba[o]} ${rgba[o + 1]} ${rgba[o + 2]}`);
  }
  writeFileSync(path, rows.join("\n"));
}

for (const n of [
  "map_kuang.gif",
  "map_kuang2.gif",
  "UISelect1.gif",
  "UISelect2.gif",
  "UI8.gif",
  "UI9.gif",
  "UI10.gif",
  "UI11.gif",
  "UI12.gif",
  "UI13.gif",
  "UI14.gif",
  "UI15.gif",
]) {
  try {
    const g = decodeLkv2Gif(unwrapResource(readFileSync(`../web/fs/JHOnlineData/${n}`)));
    console.log(n, `${g.width}x${g.height}`);
    writePpm(`tmp-map-ui/${n.replace(/\.gif$/i, "")}.ppm`, g.width, g.height, g.rgba);
  } catch (e) {
    console.log(n, "FAIL", (e as Error).message);
  }
}
