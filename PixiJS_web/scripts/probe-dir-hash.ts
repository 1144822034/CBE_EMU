import fs from "node:fs";
import { unwrapResource } from "../src/resources/lzss.ts";
import { decodeLkv2Gif } from "../src/resources/lkv2Gif.ts";
import {
  parseActorPayload,
  composePartStacked,
  type LoadedActor,
} from "../src/resources/actor.ts";

function hashFrame(rgba: Uint8ClampedArray): string {
  let h = 2166136261;
  for (let i = 0; i < rgba.length; i += 16) {
    h ^= rgba[i]!;
    h = Math.imul(h, 16777619);
  }
  return (h >>> 0).toString(16);
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

const a = await load("hW_warrior.actor");
for (const ai of [0, 1, 2, 3, 8, 9]) {
  const anim = a.animations[ai];
  if (!anim) continue;
  console.log(`--- anim${ai} parts=${anim.parts.length} avgLayers=${(anim.parts.reduce((s, p) => s + p.frames.length, 0) / anim.parts.length).toFixed(1)}`);
  for (let pi = 0; pi < anim.parts.length; pi++) {
    const c = composePartStacked(a, ai, pi);
    if (!c) {
      console.log(`  p${pi} null`);
      continue;
    }
    console.log(`  p${pi} ${c.width}x${c.height} hash=${hashFrame(c.rgba)} layers=${anim.parts[pi]!.frames.length}`);
  }
}
