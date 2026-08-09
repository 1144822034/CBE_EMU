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

function layerImgs(a: LoadedActor, ai: number, pi: number) {
  const part = a.animations[ai]?.parts[pi];
  if (!part) return [];
  return part.frames.map((f) => {
    const r = a.rects[f.rectIndex]!;
    return {
      img: a.imageNames[r.imageIndex],
      w: r.right - r.left,
      h: r.bottom - r.top,
      ox: f.offsetX,
      oy: f.offsetY,
    };
  });
}

function ascii(
  c: { width: number; height: number; rgba: Uint8ClampedArray },
  label: string,
) {
  console.log("====", label, `${c.width}x${c.height}`);
  const sx = Math.max(1, Math.floor(c.width / 28));
  const sy = Math.max(1, Math.floor(c.height / 12));
  for (let y = 0; y < c.height; y += sy) {
    let row = "";
    for (let x = 0; x < c.width; x += sx) {
      const i = (y * c.width + x) * 4;
      if (c.rgba[i + 3]! < 40) row += " ";
      else row += "#";
    }
    console.log(row);
  }
}

const war = await load("hW_warrior.actor");

for (const ai of [4, 5]) {
  console.log(`\n=== anim${ai} parts layer sources ===`);
  for (let p = 0; p < war.animations[ai]!.parts.length; p++) {
    const layers = layerImgs(war, ai, p);
    const imgs = layers.map((l) => l.img).join("+");
    const c = composePartStacked(war, ai, p)!;
    console.log(
      `p${p}`,
      `${c.width}x${c.height}`,
      `L${layers.length}`,
      imgs,
    );
  }
}

// Compose side WITHOUT jian.gif / guang — body+shadow only
function composeWithout(a: LoadedActor, ai: number, pi: number, skip: RegExp) {
  const part = a.animations[ai]!.parts[pi]!;
  const kept = part.frames.filter((f) => {
    const r = a.rects[f.rectIndex]!;
    return !skip.test(a.imageNames[r.imageIndex] || "");
  });
  // reuse by temporarily swapping
  const orig = part.frames;
  (part as { frames: typeof orig }).frames = kept;
  const c = composePartStacked(a, ai, pi);
  part.frames = orig;
  return c;
}

console.log("\n=== a4/a5 without jian/guang ===");
for (const ai of [4, 5]) {
  for (const p of [0, 2, 4, 6]) {
    const c = composeWithout(war, ai, p, /jian|guang/i);
    if (c) ascii(c, `a${ai}p${p} body`);
  }
}

// Check walk1.gif sheet layout — how many cells?
const walk1 = war.images[0]!;
console.log("\nwalk1 sheet", walk1!.width + "x" + walk1!.height);
const walk2 = war.images[1]!;
console.log("walk2 sheet", walk2!.width + "x" + walk2!.height);

// Unique rect sizes on walk sheets
const rectSummary = new Map<string, number>();
for (const r of war.rects) {
  if (r.imageIndex > 1) continue;
  const key = `${war.imageNames[r.imageIndex]} ${r.right - r.left}x${r.bottom - r.top} @${r.left},${r.top}`;
  rectSummary.set(key, (rectSummary.get(key) || 0) + 1);
}
console.log("walk rects sample:");
let n = 0;
for (const [k, v] of rectSummary) {
  console.log(" ", k, "x" + v);
  if (++n > 30) break;
}
