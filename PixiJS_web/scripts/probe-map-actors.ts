import fs from "node:fs";
import path from "node:path";
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

function writePpm(file: string, w: number, h: number, rgba: Uint8ClampedArray) {
  const lines = [`P3\n${w} ${h}\n255\n`];
  for (let i = 0; i < rgba.length; i += 4) {
    const a = rgba[i + 3]!;
    if (a < 16) lines.push("0 0 0 ");
    else lines.push(`${rgba[i]} ${rgba[i + 1]} ${rgba[i + 2]} `);
  }
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, lines.join(""));
}

function hash(rgba: Uint8ClampedArray): string {
  let h = 2166136261;
  for (let i = 0; i < rgba.length; i += 8) {
    h ^= rgba[i]!;
    h = Math.imul(h, 16777619);
  }
  return (h >>> 0).toString(16);
}

const names = [
  "e_bat.actor",
  "e_ghostfire.actor",
  "e_wolf.actor",
  "n_huangrong.actor",
  "n_blacksmith.actor",
  "hW_warrior.actor",
];

for (const name of names) {
  try {
    const a = await load(name);
    const plan = planMapAnims(a);
    console.log(
      name,
      "anims",
      a.animations.length,
      "parts0",
      a.animations[0]?.parts.length,
      "plan",
      JSON.stringify(plan),
    );
    for (const dir of [1, 2, 3, 4]) {
      const c = composeMapActor(a, dir, false, 0);
      const w = composeMapActor(a, dir, true, 0);
      console.log(
        "  dir",
        dir,
        "stand",
        c ? `${c.width}x${c.height} ${hash(c.rgba)}` : "null",
        "walk",
        w ? `${w.width}x${w.height} ${hash(w.rgba)}` : "null",
      );
    }
  } catch (e) {
    console.log(name, "FAIL", e instanceof Error ? e.message : e);
  }
}

// Dump stand anim part0 for warrior locomotion anims + all even parts of anim1
const war = await load("hW_warrior.actor");
const out = "tmp-actor-poses/dir-probe";
for (const ai of [0, 1, 2, 3, 8, 9]) {
  const c = composePartStacked(war, ai, 0);
  if (c) writePpm(`${out}/a${ai}_p0.ppm`, c.width, c.height, c.rgba);
}
for (let p = 0; p < 8; p++) {
  const c = composePartStacked(war, 1, p);
  if (c) writePpm(`${out}/stand_p${p}.ppm`, c.width, c.height, c.rgba);
}
console.log("wrote", out);

// Compare unique hashes across anim stand p0
console.log("\nunique stand a*p0:");
const seen = new Map<string, string>();
for (const ai of [1, 3, 8, 9]) {
  const c = composePartStacked(war, ai, 0)!;
  const h = hash(c.rgba);
  console.log(`a${ai}`, h, seen.has(h) ? `= ${seen.get(h)}` : "NEW");
  if (!seen.has(h)) seen.set(h, `a${ai}`);
}
