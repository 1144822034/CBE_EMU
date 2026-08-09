import fs from "node:fs";
import { unwrapResource } from "../src/resources/lzss.ts";
import { decodeLkv2Gif } from "../src/resources/lkv2Gif.ts";

const names = [
  "UIsh1.gif",
  "UIsh2.gif",
  "UIsh3.gif",
  "UIsh4.gif",
  "UIsh5.gif",
  "UIsh6.gif",
  "UI2.gif",
  "UI7.gif",
  "UIxia1.gif",
  "UIxia3.gif",
  "UIRed.gif",
  "training.gif",
  "exp0.gif",
  "exp1.gif",
  "vip_effect.gif",
];
for (const n of names) {
  try {
    const g = decodeLkv2Gif(unwrapResource(fs.readFileSync(`../bin/JHOnlineData/${n}`)));
    console.log(n, `${g.width}x${g.height}`);
  } catch (e) {
    console.log(n, String(e).slice(0, 100));
  }
}
