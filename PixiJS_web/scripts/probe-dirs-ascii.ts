import fs from "node:fs";
import { unwrapResource } from "../src/resources/lzss.ts";
import { decodeLkv2Gif } from "../src/resources/lkv2Gif.ts";
import {
  parseActorPayload,
  composePartStacked,
  planMapAnims,
  composeMapActor,
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

function hash(rgba: Uint8ClampedArray): string {
  let h = 2166136261;
  for (let i = 0; i < rgba.length; i += 8) {
    h ^= rgba[i]!;
    h = Math.imul(h, 16777619);
  }
  return (h >>> 0).toString(16);
}

function ascii(
  c: { width: number; height: number; rgba: Uint8ClampedArray },
  label: string,
) {
  console.log("====", label, c.width + "x" + c.height, hash(c.rgba));
  const sx = Math.max(1, Math.floor(c.width / 20));
  const sy = Math.max(1, Math.floor(c.height / 16));
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
        if (r > 160 && g > 120 && b < 100) row += "O";
        else if (r + g + b > 400) row += "#";
        else row += ".";
      }
    }
    console.log(row);
  }
}

const war = await load("hW_warrior.actor");
for (const ai of [1, 3, 8, 9]) {
  const c = composePartStacked(war, ai, 0);
  if (c) ascii(c, `a${ai}p0`);
}
for (const p of [0, 2, 4, 6]) {
  const c = composePartStacked(war, 1, p);
  if (c) ascii(c, `stand a1 p${p}`);
}

for (const name of [
  "e_ghostfireR.actor",
  "e_batB.actor",
  "e_boar.actor",
  "e_craw.actor",
]) {
  const a = await load(name);
  console.log(
    name,
    "anims",
    a.animations.length,
    "parts",
    a.animations.map((x) => x.parts.length).join(","),
    "plan",
    JSON.stringify(planMapAnims(a)),
  );
  for (const dir of [1, 2, 3, 4]) {
    const c = composeMapActor(a, dir, false, 0);
    const w = composeMapActor(a, dir, true, 1);
    console.log(
      " ",
      dir,
      c ? `${c.width}x${c.height} ${hash(c.rgba)}` : "null",
      w ? hash(w.rgba) : "null",
    );
  }
}
