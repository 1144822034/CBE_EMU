import fs from "node:fs";
import { unwrapResource } from "../src/resources/lzss.ts";
import { decodeLkv2Gif } from "../src/resources/lkv2Gif.ts";
import {
  parseActorPayload,
  composePartStacked,
  type LoadedActor,
} from "../src/resources/actor.ts";
import { createCanvas } from "node:canvas";

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

// Save without node-canvas — write raw PPM
function writePpm(path: string, w: number, h: number, rgba: Uint8ClampedArray) {
  const lines = [`P3\n${w} ${h}\n255\n`];
  for (let i = 0; i < w * h; i++) {
    const o = i * 4;
    const a = rgba[o + 3]!;
    if (a < 128) lines.push("0 0 0 ");
    else lines.push(`${rgba[o]} ${rgba[o + 1]} ${rgba[o + 2]} `);
    if (i % w === w - 1) lines.push("\n");
  }
  fs.writeFileSync(path, lines.join(""));
}

const a = await load("hW_warrior.actor");
console.log("partIds anim1:", a.animations[1]!.parts.map((p) => p.partId).join(","));
console.log("partIds anim0:", a.animations[0]!.parts.map((p) => p.partId).join(","));

// For stand anims 1,3,8,9 dump even parts
const outDir = "tmp-actor-poses";
fs.mkdirSync(outDir, { recursive: true });
for (const ai of [1, 3, 8, 9]) {
  for (const pi of [0, 2, 4, 6]) {
    const c = composePartStacked(a, ai, pi);
    if (!c) continue;
    writePpm(`${outDir}/stand_a${ai}_p${pi}.ppm`, c.width, c.height, c.rgba);
    console.log(`wrote a${ai}p${pi} ${c.width}x${c.height}`);
  }
}
