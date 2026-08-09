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
  const sx = Math.max(1, Math.floor(c.width / 22));
  const sy = Math.max(1, Math.floor(c.height / 12));
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

// jian offsets per part in a4/a5
for (const ai of [4, 5]) {
  console.log(`\nanim${ai} jian offsets:`);
  for (let pi = 0; pi < 8; pi++) {
    const jian = a.animations[ai]!.parts[pi]!.frames
      .map((f) => {
        const r = a.rects[f.rectIndex]!;
        const name = a.imageNames[r.imageIndex] || "";
        if (!/jian/i.test(name)) return null;
        return `${r.right - r.left}x${r.bottom - r.top}@off${f.offsetX},${f.offsetY}`;
      })
      .filter(Boolean);
    const c = composePartStacked(a, ai, pi)!;
    console.log(` p${pi}`, c.width + "x" + c.height, jian.join(" | ") || "no-jian");
  }
}

// Compare female - reference is female facing right
const f = await load("hW_warrior.actor");
console.log("\n--- female stand/walk side candidates ---");
// With jian (full CBE pose)
for (const [ai, label] of [
  [5, "a5=right?"],
  [4, "a4=left?"],
] as const) {
  ascii(composePartStacked(f, ai, 0)!, `${label} full p0`);
  ascii(composePartStacked(f, ai, 4)!, `${label} full p4`);
}

// Also check: maybe CBE map uses anim index = dir, and frame time within part0 only via composeAnimationFrame differently

// Hypothesis from CBE dir codes 1..4 and anim pairs:
// down:0/1 up:2/3  and side walk might be flipping FRONT (a0/a1) — check size vs reference side
ascii(composePartStacked(f, 1, 0)!, "front stand");
ascii(composePartStacked(f, 3, 0)!, "back stand");
