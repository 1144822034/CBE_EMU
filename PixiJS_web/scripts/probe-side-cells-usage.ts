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
  console.log("====", label, c.width + "x" + c.height);
  const sx = Math.max(1, Math.floor(c.width / 20));
  const sy = Math.max(1, Math.floor(c.height / 14));
  for (let y = 0; y < c.height; y += sy) {
    let row = "";
    for (let x = 0; x < c.width; x += sx) {
      const i = (y * c.width + x) * 4;
      row += c.rgba[i + 3]! < 40 ? " " : "#";
    }
    console.log(row);
  }
}

const a = await load("hW_warrior.actor");
const walk2Idx = a.imageNames.findIndex((n) => /walk2/i.test(n));
console.log("walk2 imageIndex", walk2Idx);

// Find all frames that use walk2 side cells (left>=30)
type Hit = { ai: number; pi: number; fi: number; left: number; top: number; w: number; h: number };
const hits: Hit[] = [];
for (let ai = 0; ai < a.animations.length; ai++) {
  for (let pi = 0; pi < a.animations[ai]!.parts.length; pi++) {
    a.animations[ai]!.parts[pi]!.frames.forEach((f, fi) => {
      const r = a.rects[f.rectIndex]!;
      if (r.imageIndex !== walk2Idx) return;
      if (r.left < 28) return;
      hits.push({
        ai,
        pi,
        fi,
        left: r.left,
        top: r.top,
        w: r.right - r.left,
        h: r.bottom - r.top,
      });
    });
  }
}
console.log("walk2 side-cell usages", hits.length);
const byAnim = new Map<string, number>();
for (const h of hits) {
  const k = `a${h.ai}p${h.pi}@${h.left}`;
  byAnim.set(k, (byAnim.get(k) || 0) + 1);
}
for (const [k, v] of [...byAnim.entries()].sort()) console.log(" ", k, "x" + v);

// Compose every even part of anim0-5 and see which look side-profile (width/height or centroid)
for (const ai of [0, 1, 2, 3, 4, 5]) {
  for (const pi of [0, 2, 4, 6]) {
    const c = composePartStacked(a, ai, pi);
    if (!c) continue;
    let sx = 0,
      n = 0;
    for (let y = 0; y < c.height; y++)
      for (let x = 0; x < c.width; x++)
        if (c.rgba[(y * c.width + x) * 4 + 3]! > 40) {
          sx += x;
          n++;
        }
    const cx = n ? sx / n : 0;
    const skew = cx - c.width / 2;
    const usesSide = hits.some((h) => h.ai === ai && h.pi === pi);
    if (usesSide || Math.abs(skew) > 1.5 || ai >= 4)
      console.log(
        `a${ai}p${pi}`,
        `${c.width}x${c.height}`,
        "skew",
        skew.toFixed(1),
        usesSide ? "HAS_SIDE_CELL" : "",
      );
  }
}

// Show a0/a1 parts that have side cells
for (const h of hits.filter((x) => x.ai <= 3)) {
  /* listed above */
}
const sideParts = new Set(hits.filter((h) => h.ai <= 5).map((h) => `a${h.ai}p${h.pi}`));
console.log("\nparts with walk2 side:", [...sideParts].sort().join(" "));

for (const key of ["a0p2", "a0p6", "a1p2", "a1p6", "a4p0", "a5p0"]) {
  const m = /^a(\d+)p(\d+)$/.exec(key)!;
  const c = composePartStacked(a, +m[1]!, +m[2]!);
  if (c) ascii(c, key);
}
