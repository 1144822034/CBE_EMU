import fs from "node:fs";
import { unwrapResource } from "../src/resources/lzss.ts";
import { decodeLkv2Gif } from "../src/resources/lkv2Gif.ts";
import {
  parseActorPayload,
  composePartStacked,
  composeAnimationFrame,
  type LoadedActor,
} from "../src/resources/actor.ts";

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

function hash(rgba: Uint8ClampedArray): string {
  let h = 2166136261;
  for (let i = 0; i < rgba.length; i += 8) {
    h ^= rgba[i]!;
    h = Math.imul(h, 16777619);
  }
  return (h >>> 0).toString(16);
}

const a = await load("hW_warrior.actor");
console.log("images", a.imageNames.join(", "));

// Model B: each anim is a direction; frameIndex cycles walk; composeAnimationFrame stacks parts as layers
console.log("\n=== composeAnimationFrame (parts=layers, frame=time) ===");
for (let ai = 0; ai < 4; ai++) {
  const maxF = Math.max(...a.animations[ai]!.parts.map((p) => p.frames.length));
  for (let f = 0; f < Math.min(3, maxF); f++) {
    const c = composeAnimationFrame(a, ai, f);
    console.log(`a${ai}f${f}`, c ? `${c.width}x${c.height} ${hash(c.rgba)}` : "null");
  }
}

// Model C: anim pair per dir — use only part 0 stacked for stand of each locomotion anim
console.log("\n=== stand part0 across anims ===");
for (const ai of [1, 3, 5, 7, 8, 9]) {
  if (!a.animations[ai]) continue;
  const c = composePartStacked(a, ai, 0);
  console.log(`a${ai}p0`, c ? `${c.width}x${c.height} ${hash(c.rgba)}` : "null", "layers", avg(a.animations[ai]!));
}

function avg(anim: { parts: { frames: unknown[] }[] }) {
  return (anim.parts.reduce((s, p) => s + p.frames.length, 0) / anim.parts.length).toFixed(1);
}

// Compare flipped uniqueness: is anim1 p2 mirror of p6?
function flipHash(c: { width: number; height: number; rgba: Uint8ClampedArray }) {
  const out = new Uint8ClampedArray(c.rgba.length);
  for (let y = 0; y < c.height; y++) {
    for (let x = 0; x < c.width; x++) {
      const si = (y * c.width + x) * 4;
      const di = (y * c.width + (c.width - 1 - x)) * 4;
      out[di] = c.rgba[si]!;
      out[di + 1] = c.rgba[si + 1]!;
      out[di + 2] = c.rgba[si + 2]!;
      out[di + 3] = c.rgba[si + 3]!;
    }
  }
  return hash(out);
}
const p2 = composePartStacked(a, 1, 2)!;
const p6 = composePartStacked(a, 1, 6)!;
console.log("\nanim1 p2 hash", hash(p2.rgba), "flip", flipHash(p2));
console.log("anim1 p6 hash", hash(p6.rgba), "match flip?", flipHash(p2) === hash(p6.rgba));
