/**
 * Find which side-facing part looks like 收脚 stand vs stride.
 * Writes PNGs under tmp-actor-poses/side-idle/
 */
import fs from "node:fs";
import zlib from "node:zlib";
import { unwrapResource } from "../src/resources/lzss.ts";
import { decodeLkv2Gif } from "../src/resources/lkv2Gif.ts";
import {
  parseActorPayload,
  composePartStacked,
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

function footCols(c: ComposedFrame): { left: number; right: number; width: number; gaps: number } {
  const y0 = Math.floor(c.height * 0.75);
  const colHas: boolean[] = [];
  for (let x = 0; x < c.width; x++) {
    let hit = false;
    for (let y = y0; y < c.height; y++) {
      if (c.rgba[(y * c.width + x) * 4 + 3]! >= 40) {
        hit = true;
        break;
      }
    }
    colHas[x] = hit;
  }
  let left = -1,
    right = -1,
    gaps = 0;
  let inGap = false;
  let seen = false;
  for (let x = 0; x < c.width; x++) {
    if (!colHas[x]) {
      if (seen) inGap = true;
      continue;
    }
    if (left < 0) left = x;
    right = x;
    if (inGap) {
      gaps++;
      inGap = false;
    }
    seen = true;
  }
  return {
    left,
    right,
    width: left < 0 ? 0 : right - left + 1,
    gaps,
  };
}

function bodyOnly(a: LoadedActor, ai: number, pi: number): ComposedFrame | null {
  const part = a.animations[ai]?.parts[pi];
  if (!part) return null;
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

const out = "tmp-actor-poses/side-idle";
fs.mkdirSync(out, { recursive: true });
const a = await load("hW_warrior.actor");

for (const ai of [0, 2, 4, 5]) {
  const n = a.animations[ai]!.parts.length;
  console.log(`=== anim${ai} n=${n} ===`);
  for (let pi = 0; pi < n; pi++) {
    const c = bodyOnly(a, ai, pi);
    if (!c) continue;
    const f = footCols(c);
    console.log(
      `a${ai}p${pi}`,
      `${c.width}x${c.height}`,
      `footW=${f.width}`,
      `footGaps=${f.gaps}`,
      `x=${f.left}..${f.right}`,
    );
    const s = scale(c, 4);
    fs.writeFileSync(`${out}/a${ai}_p${pi}.png`, rgbaToPng(s.w, s.h, s.rgba));
  }
}
