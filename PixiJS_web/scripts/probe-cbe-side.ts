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

function flipH(c: { width: number; height: number; rgba: Uint8ClampedArray }) {
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
  return out;
}

function hash(rgba: Uint8ClampedArray): string {
  let h = 2166136261;
  for (let i = 0; i < rgba.length; i += 8) {
    h ^= rgba[i]!;
    h = Math.imul(h, 16777619);
  }
  return (h >>> 0).toString(16);
}

function bodyOnly(a: LoadedActor, ai: number, pi: number) {
  const part = a.animations[ai]!.parts[pi]!;
  const kept = part.frames.filter((f) => {
    const name = a.imageNames[a.rects[f.rectIndex]!.imageIndex] || "";
    return !/jian|guang|death/i.test(name);
  });
  const orig = part.frames;
  (part as { frames: typeof orig }).frames = kept;
  const c = composePartStacked(a, ai, pi);
  part.frames = orig;
  return c;
}

const a = await load("hW_warrior.actor");
const a4 = bodyOnly(a, 4, 0)!;
const a5 = bodyOnly(a, 5, 0)!;
console.log("a4", a4.width + "x" + a4.height, hash(a4.rgba));
console.log("a5", a5.width + "x" + a5.height, hash(a5.rgba));
console.log("a4flip==a5", hash(flipH(a4)) === hash(a5.rgba));
console.log("a5flip==a4", hash(flipH(a5)) === hash(a4.rgba));

// Rect sources for stand front vs side body first layer centroids
function describe(ai: number, pi: number, label: string) {
  const part = a.animations[ai]!.parts[pi]!;
  console.log(label, "layers", part.frames.length);
  for (const f of part.frames.slice(0, 6)) {
    const r = a.rects[f.rectIndex]!;
    console.log(
      " ",
      a.imageNames[r.imageIndex],
      `${r.right - r.left}x${r.bottom - r.top}@${r.left},${r.top}`,
      `off ${f.offsetX},${f.offsetY}`,
    );
  }
}
describe(1, 0, "stand-front a1p0");
describe(3, 0, "stand-back a3p0");
describe(4, 0, "side-a4p0");
describe(0, 0, "walk-front a0p0");

// Dump walk1 sheet as ascii thumbnail
const sheet = a.images[0]!;
console.log("walk1", sheet!.width + "x" + sheet!.height);
const sx = Math.max(1, Math.floor(sheet!.width / 40));
const sy = Math.max(1, Math.floor(sheet!.height / 20));
for (let y = 0; y < sheet!.height; y += sy) {
  let row = "";
  for (let x = 0; x < sheet!.width; x += sx) {
    const i = (y * sheet!.width + x) * 4;
    row += sheet!.rgba[i + 3]! < 40 ? " " : "#";
  }
  console.log(row);
}
