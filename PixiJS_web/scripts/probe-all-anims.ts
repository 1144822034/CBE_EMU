import fs from "node:fs";
import { unwrapResource } from "../src/resources/lzss.ts";
import { decodeLkv2Gif } from "../src/resources/lkv2Gif.ts";
import {
  parseActorPayload,
  composePartStacked,
  type LoadedActor,
} from "../src/resources/actor.ts";

async function load(name: string): Promise<LoadedActor> {
  const parsed = parseActorPayload(
    unwrapResource(fs.readFileSync(`../bin/JHOnlineData/${name}`)),
    name,
  );
  const images = await Promise.all(
    parsed.imageNames.map((gif) => {
      try {
        return decodeLkv2Gif(
          unwrapResource(fs.readFileSync(`../bin/JHOnlineData/${gif}`)),
        );
      } catch {
        return null;
      }
    }),
  );
  return { ...parsed, images };
}

function ascii(
  c: { width: number; height: number; rgba: Uint8ClampedArray },
  label: string,
) {
  console.log("====", label, `${c.width}x${c.height}`);
  const sx = Math.max(1, Math.floor(c.width / 22));
  const sy = Math.max(1, Math.floor(c.height / 14));
  for (let y = 0; y < c.height; y += sy) {
    let row = "";
    for (let x = 0; x < c.width; x += sx) {
      const i = (y * c.width + x) * 4;
      const a = c.rgba[i + 3]!;
      if (a < 40) row += " ";
      else {
        const r = c.rgba[i]!,
          g = c.rgba[i + 1]!,
          b = c.rgba[i + 2]!;
        if (r > 180 && g < 100 && b < 100) row += "R";
        else if (r + g + b > 420) row += "#";
        else row += ".";
      }
    }
    console.log(row);
  }
}

function avgLayers(a: LoadedActor, ai: number): string {
  const anim = a.animations[ai];
  if (!anim) return "-";
  const n =
    anim.parts.reduce((s, p) => s + p.frames.length, 0) / Math.max(1, anim.parts.length);
  return `${anim.parts.length}p L${n.toFixed(1)}`;
}

const war = await load("hW_warrior.actor");
console.log("images", war.imageNames.join(", "));
console.log("anims", war.animations.length);
for (let ai = 0; ai < war.animations.length; ai++) {
  console.log(` a${ai}`, avgLayers(war, ai));
}

// Dump part0 of every anim for visual ID
for (let ai = 0; ai < war.animations.length; ai++) {
  const c = composePartStacked(war, ai, 0);
  if (c) ascii(c, `a${ai}p0`);
}

// Within anim0/1/2/3: compare even parts — maybe side is a part not anim8/9
console.log("\n--- anim0 even parts sizes ---");
for (const ai of [0, 1, 2, 3]) {
  const sizes = [0, 2, 4, 6].map((p) => {
    const c = composePartStacked(war, ai, p);
    return c ? `${c.width}x${c.height}` : "null";
  });
  console.log(`a${ai}`, sizes.join(" "));
}
