import fs from "node:fs";
import zlib from "node:zlib";
import { unwrapResource } from "../src/resources/lzss.ts";
import { decodeLkv2Gif } from "../src/resources/lkv2Gif.ts";
import {
  parseActorPayload,
  composePartStacked,
  composeMapActor,
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

function bodyOnly(a: LoadedActor, ai: number, pi: number): ComposedFrame | null {
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

function write(tag: string, c: ComposedFrame | null) {
  if (!c) {
    console.log("skip", tag);
    return;
  }
  const s = scale(c, 4);
  fs.writeFileSync(
    `tmp-actor-poses/dir-fix/${tag}.png`,
    rgbaToPng(s.w, s.h, s.rgba),
  );
  console.log(tag, c.width + "x" + c.height, hash(c.rgba));
}

fs.mkdirSync("tmp-actor-poses/dir-fix", { recursive: true });
const a = await load("hW_warrior.actor");

for (let ai = 0; ai < 10; ai++) {
  write(`a${ai}_full`, composePartStacked(a, ai, 0));
  write(`a${ai}_body`, bodyOnly(a, ai, 0));
}

console.log("--- current map ---");
for (const [k, d] of [
  ["W", 1],
  ["D", 2],
  ["S", 3],
  ["A", 4],
] as const) {
  write(`map_${k}_stand`, composeMapActor(a, d, false, 0));
  write(`map_${k}_walk0`, composeMapActor(a, d, true, 0));
  write(`map_${k}_walk1`, composeMapActor(a, d, true, 1));
}
