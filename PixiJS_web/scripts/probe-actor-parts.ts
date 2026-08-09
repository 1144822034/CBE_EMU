import fs from "node:fs";
import { unwrapResource } from "../src/resources/lzss";
import { decodeLkv2Gif } from "../src/resources/lkv2Gif";
import { parseActorPayload } from "../src/resources/actor";

const raw = unwrapResource(new Uint8Array(fs.readFileSync("../bin/JHOnlineData/h_warrior.actor")));
const a = parseActorPayload(raw, "h_warrior.actor");
for (const name of a.imageNames) {
  const g = decodeLkv2Gif(unwrapResource(new Uint8Array(fs.readFileSync("../bin/JHOnlineData/" + name))));
  console.log(name, g.width + "x" + g.height);
}
console.log("--- rects area>=200 ---");
a.rects.forEach((r, i) => {
  const w = r.right - r.left;
  const h = r.bottom - r.top;
  if (w * h >= 200) console.log(i, `img${r.imageIndex}`, `${w}x${h}`, `@${r.left},${r.top}`);
});
// which anim frames reference large rects?
console.log("--- frames hitting large rects ---");
a.animations.forEach((anim, ai) => {
  anim.parts.forEach((p) => {
    p.frames.forEach((f, fi) => {
      const r = a.rects[f.rectIndex]!;
      const area = (r.right - r.left) * (r.bottom - r.top);
      if (area >= 200) console.log(`anim${ai} part${p.partId} f${fi} rect${f.rectIndex} ${r.right - r.left}x${r.bottom - r.top}`);
    });
  });
});
