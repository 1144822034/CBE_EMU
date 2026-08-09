import fs from "node:fs";
import { unwrapResource } from "../src/resources/lzss.ts";
import { decodeLkv2Gif } from "../src/resources/lkv2Gif.ts";
import {
  parseActorPayload,
  composeActorFrame,
  type LoadedActor,
  type ActorPart,
} from "../src/resources/actor.ts";

function composePartLayers(actor: LoadedActor, part: ActorPart) {
  type L = { ox: number; oy: number; w: number; h: number; rgba: Uint8ClampedArray };
  const layers: L[] = [];
  for (const f of part.frames) {
    const c = composeActorFrame(actor, f);
    if (!c || c.width * c.height < 4) continue;
    layers.push({ ox: f.offsetX, oy: f.offsetY, w: c.width, h: c.height, rgba: c.rgba });
  }
  if (!layers.length) return null;
  let minX = Infinity,
    minY = Infinity,
    maxX = -Infinity,
    maxY = -Infinity;
  for (const l of layers) {
    minX = Math.min(minX, l.ox);
    minY = Math.min(minY, l.oy);
    maxX = Math.max(maxX, l.ox + l.w);
    maxY = Math.max(maxY, l.oy + l.h);
  }
  const width = maxX - minX;
  const height = maxY - minY;
  const rgba = new Uint8ClampedArray(width * height * 4);
  for (const l of layers) {
    const dx = l.ox - minX;
    const dy = l.oy - minY;
    for (let y = 0; y < l.h; y++) {
      for (let x = 0; x < l.w; x++) {
        const si = (y * l.w + x) * 4;
        if (!l.rgba[si + 3]) continue;
        const di = ((dy + y) * width + (dx + x)) * 4;
        rgba[di] = l.rgba[si]!;
        rgba[di + 1] = l.rgba[si + 1]!;
        rgba[di + 2] = l.rgba[si + 2]!;
        rgba[di + 3] = l.rgba[si + 3]!;
      }
    }
  }
  return { width, height, layers: layers.length };
}

async function load(name: string): Promise<LoadedActor> {
  const parsed = parseActorPayload(unwrapResource(fs.readFileSync(`../bin/JHOnlineData/${name}`)), name);
  const images = await Promise.all(
    parsed.imageNames.map((gif) => {
      try {
        return decodeLkv2Gif(unwrapResource(fs.readFileSync(`../bin/JHOnlineData/${gif}`)));
      } catch {
        return null;
      }
    }),
  );
  return { ...parsed, images };
}

for (const name of ["hW_warrior.actor", "e_ghostfireB.actor", "e_batB.actor", "FB_NPC.actor"]) {
  const a = await load(name);
  console.log("====", name);
  for (let ai = 0; ai < Math.min(4, a.animations.length); ai++) {
    const parts = a.animations[ai]!.parts;
    const line = parts.map((p, i) => {
      const c = composePartLayers(a, p);
      return c ? `p${i}:${c.width}x${c.height}L${c.layers}` : `p${i}:null`;
    });
    console.log(` anim${ai}`, line.join(" "));
  }
}
