import fs from "node:fs";
import zlib from "node:zlib";
import { unwrapResource } from "../src/resources/lzss.ts";
import { decodeLkv2Gif } from "../src/resources/lkv2Gif.ts";
import {
  parseActorPayload,
  composePartStacked,
  composeMapActor,
  planMapAnims,
  type LoadedActor,
  type ComposedFrame,
} from "../src/resources/actor.ts";

function crc32(buf: Buffer): number {
  let c = ~0;
  for (let i = 0; i < buf.length; i++) {
    c ^= buf[i]!;
    for (let k = 0; k < 8; k++) c = (c >>> 1) ^ (0xedb88320 & -(c & 1));
  }
  return ~c >>> 0;
}

function rgbaToPng(w: number, h: number, rgba: Uint8ClampedArray): Buffer {
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) {
    raw[y * (w * 4 + 1)] = 0;
    Buffer.from(rgba.buffer, rgba.byteOffset + y * w * 4, w * 4).copy(
      raw,
      y * (w * 4 + 1) + 1,
    );
  }
  const chunk = (t: string, d: Buffer) => {
    const len = Buffer.alloc(4);
    len.writeUInt32BE(d.length);
    const type = Buffer.from(t);
    const crc = Buffer.alloc(4);
    crc.writeUInt32BE(crc32(Buffer.concat([type, d])));
    return Buffer.concat([len, type, d, crc]);
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0);
  ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8;
  ihdr[9] = 6;
  return Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk("IHDR", ihdr),
    chunk("IDAT", zlib.deflateSync(raw)),
    chunk("IEND", Buffer.alloc(0)),
  ]);
}

function scale(c: ComposedFrame, s: number) {
  const w = c.width * s;
  const h = c.height * s;
  const rgba = new Uint8ClampedArray(w * h * 4);
  for (let y = 0; y < c.height; y++) {
    for (let x = 0; x < c.width; x++) {
      const si = (y * c.width + x) * 4;
      for (let dy = 0; dy < s; dy++) {
        for (let dx = 0; dx < s; dx++) {
          const di = ((y * s + dy) * w + (x * s + dx)) * 4;
          rgba[di] = c.rgba[si]!;
          rgba[di + 1] = c.rgba[si + 1]!;
          rgba[di + 2] = c.rgba[si + 2]!;
          rgba[di + 3] = c.rgba[si + 3]!;
        }
      }
    }
  }
  return { w, h, rgba };
}

function hash(rgba: Uint8ClampedArray): string {
  let h = 2166136261;
  for (let i = 0; i < rgba.length; i += 8) {
    h ^= rgba[i]!;
    h = Math.imul(h, 16777619);
  }
  return (h >>> 0).toString(16);
}

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

function write(dir: string, tag: string, c: ComposedFrame | null) {
  if (!c) return;
  const s = scale(c, 4);
  fs.writeFileSync(`${dir}/${tag}.png`, rgbaToPng(s.w, s.h, s.rgba));
}

const candidates = fs
  .readdirSync("../bin/JHOnlineData")
  .filter((f) => /^e_.*\.actor$/i.test(f))
  .sort();

console.log("e_ actors", candidates.length);

const focus = [
  "e_ghostfireR.actor",
  "e_ghostfireG.actor",
  "e_batB.actor",
  "e_duNi.actor",
  "e_huLang.actor",
  "e_beesY.actor",
  "e_slimeG.actor",
  "e_lang.actor",
  "e_she.actor",
].filter((n) => fs.existsSync(`../bin/JHOnlineData/${n}`));

// Prefer map sprites from SCE if listed elsewhere; always include focus + a few more
const pick = [
  ...focus,
  ...candidates.filter((n) => !focus.includes(n)).slice(0, 8),
];

for (const name of pick) {
  const a = await load(name);
  const plan = planMapAnims(a);
  console.log(`\n=== ${name} anims=${a.animations.length} plan=${JSON.stringify(plan)}`);
  console.log("  images", a.imageNames.join(","));
  for (let ai = 0; ai < a.animations.length; ai++) {
    const parts = a.animations[ai]!.parts;
    const sizes = parts.map((p, pi) => {
      const c = composePartStacked(a, ai, pi);
      return c ? `${c.width}x${c.height}:${hash(c.rgba).slice(0, 6)}` : "null";
    });
    console.log(
      `  a${ai} parts=${parts.length} uniq=${new Set(sizes).size}`,
      sizes.join(" | "),
    );
  }
  const out = `tmp-actor-poses/monsters/${name.replace(/\.actor$/i, "")}`;
  fs.mkdirSync(out, { recursive: true });
  for (const d of [1, 2, 3, 4]) {
    const stand = composeMapActor(a, d, false, 0);
    const walk = composeMapActor(a, d, true, 0);
    console.log(
      `  map d${d} stand=${stand ? stand.width + "x" + stand.height + ":" + hash(stand.rgba).slice(0, 6) : "null"}`,
      `walk=${walk ? walk.width + "x" + walk.height + ":" + hash(walk.rgba).slice(0, 6) : "null"}`,
    );
    write(out, `d${d}_stand`, stand);
    write(out, `d${d}_walk`, walk);
  }
  // dump raw parts of stand anim
  const sai = plan.standAnim;
  const n = a.animations[sai]?.parts.length ?? 0;
  for (let pi = 0; pi < n; pi++) {
    write(out, `stand_a${sai}_p${pi}`, composePartStacked(a, sai, pi));
  }
}
