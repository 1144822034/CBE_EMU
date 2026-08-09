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
  const sx = Math.max(1, Math.floor(c.width / 24));
  const sy = Math.max(1, Math.floor(c.height / 12));
  for (let y = 0; y < c.height; y += sy) {
    let row = "";
    for (let x = 0; x < c.width; x += sx) {
      const i = (y * c.width + x) * 4;
      if (c.rgba[i + 3]! < 40) row += " ";
      else row += c.rgba[i]! > 160 && c.rgba[i + 1]! < 100 ? "R" : "#";
    }
    console.log(row);
  }
}

function bbox(c: { width: number; height: number; rgba: Uint8ClampedArray }) {
  let minX = c.width,
    maxX = 0,
    minY = c.height,
    maxY = 0,
    n = 0;
  for (let y = 0; y < c.height; y++) {
    for (let x = 0; x < c.width; x++) {
      if (c.rgba[(y * c.width + x) * 4 + 3]! < 40) continue;
      n++;
      minX = Math.min(minX, x);
      maxX = Math.max(maxX, x);
      minY = Math.min(minY, y);
      maxY = Math.max(maxY, y);
    }
  }
  return { n, w: maxX - minX + 1, h: maxY - minY + 1, minX, maxX, minY, maxY };
}

const war = await load("hW_warrior.actor");

console.log("=== anim1 all parts (stand) ===");
for (let p = 0; p < 8; p++) {
  const c = composePartStacked(war, 1, p)!;
  const b = bbox(c);
  ascii(c, `a1p${p} bbox ${b.w}x${b.h} n=${b.n}`);
}

console.log("\n=== anim4/5 part0 vs part2 (maybe side) ===");
for (const ai of [4, 5]) {
  for (const p of [0, 2, 4, 6]) {
    const c = composePartStacked(war, ai, p);
    if (!c) continue;
    const b = bbox(c);
    console.log(`a${ai}p${p}`, `${c.width}x${c.height}`, `opaque ${b.w}x${b.h}`);
  }
}

// Which anim uses death.gif?
console.log("\n=== rect imageIndex usage per anim (death=idx5?) ===");
console.log(
  "images",
  war.imageNames.map((n, i) => `${i}:${n}`).join(" "),
);
for (let ai = 0; ai < war.animations.length; ai++) {
  const imgs = new Set<number>();
  for (const part of war.animations[ai]!.parts) {
    for (const f of part.frames) {
      const r = war.rects[f.rectIndex];
      if (r) imgs.add(r.imageIndex);
    }
  }
  console.log(
    `a${ai}`,
    [...imgs]
      .sort((a, b) => a - b)
      .map((i) => `${i}:${war.imageNames[i]}`)
      .join(", "),
  );
}

// composeAnimationFrame for anim0 frames — maybe frame is dir?
console.log("\n=== composeAnimationFrame a0 first frames ===");
for (let f = 0; f < 4; f++) {
  const c = composeAnimationFrame(war, 0, f);
  if (c) ascii(c, `a0 frame ${f}`);
}
