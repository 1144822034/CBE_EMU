import fs from "node:fs";
import { unwrapResource } from "../src/resources/lzss.ts";
import { parseActorPayload } from "../src/resources/actor.ts";

function dump(name: string) {
  const a = parseActorPayload(unwrapResource(fs.readFileSync(`../bin/JHOnlineData/${name}`)), name);
  console.log("====", name);
  for (let ai = 0; ai < Math.min(4, a.animations.length); ai++) {
    const anim = a.animations[ai]!;
    console.log(` anim${ai} parts=${anim.parts.length}`);
    for (const p of anim.parts) {
      const sizes = p.frames.map((f) => {
        const r = a.rects[f.rectIndex]!;
        return `${r.right - r.left}x${r.bottom - r.top}@${f.offsetX},${f.offsetY}`;
      });
      console.log(`  part${p.partId} n=${p.frames.length}`, sizes.slice(0, 3).join(" | "), sizes.length > 3 ? "..." : "");
    }
  }
}

dump("hW_warrior.actor");
dump("e_ghostfireB.actor");
dump("e_batB.actor");
dump("FB_NPC.actor");
