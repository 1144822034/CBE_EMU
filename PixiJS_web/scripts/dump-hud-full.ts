import fs from "node:fs";
import path from "node:path";
import { unwrapResource } from "../src/resources/lzss.ts";
import { decodeLkv2Gif } from "../src/resources/lkv2Gif.ts";
import { deflateSync } from "node:zlib";

const root = path.resolve("../bin/JHOnlineData");
const out = path.resolve("tmp-ui/hud-full");
fs.mkdirSync(out, { recursive: true });

function crc32(buf: Buffer): number {
  let c = ~0;
  for (let i = 0; i < buf.length; i++) {
    c ^= buf[i]!;
    for (let k = 0; k < 8; k++) c = c & 1 ? (0xedb88320 ^ (c >>> 1)) : c >>> 1;
  }
  return ~c >>> 0;
}

function writePng(file: string, w: number, h: number, rgba: Uint8ClampedArray): void {
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) {
    raw[y * (w * 4 + 1)] = 0;
    Buffer.from(rgba.buffer, rgba.byteOffset + y * w * 4, w * 4).copy(raw, y * (w * 4 + 1) + 1);
  }
  const compressed = deflateSync(raw);
  const signature = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);
  const ihdrData = Buffer.alloc(13);
  ihdrData.writeUInt32BE(w, 0);
  ihdrData.writeUInt32BE(h, 4);
  ihdrData[8] = 8;
  ihdrData[9] = 6;
  const chunk = (type: string, data: Buffer) => {
    const len = Buffer.alloc(4);
    len.writeUInt32BE(data.length, 0);
    const typeBuf = Buffer.from(type);
    const crc = Buffer.alloc(4);
    crc.writeUInt32BE(crc32(Buffer.concat([typeBuf, data])), 0);
    return Buffer.concat([len, typeBuf, data, crc]);
  };
  fs.writeFileSync(
    file,
    Buffer.concat([signature, chunk("IHDR", ihdrData), chunk("IDAT", compressed), chunk("IEND", Buffer.alloc(0))]),
  );
}

const names = fs
  .readdirSync(root)
  .filter((n) => /\.gif$/i.test(n))
  .filter((n) =>
    /^(UI|ui_|exp|HP|bag|sword|auto|title|Background|loading|map_kuang|numberexp|equipico)/i.test(n),
  );

for (const name of names.sort()) {
  try {
    const g = decodeLkv2Gif(unwrapResource(fs.readFileSync(path.join(root, name))));
    writePng(path.join(out, name.replace(/\.gif$/i, ".png")), g.width, g.height, g.rgba);
    console.log(`${name.padEnd(24)} ${String(g.width).padStart(4)}x${String(g.height).padStart(4)}`);
  } catch (e) {
    console.log(name, "FAIL", (e as Error).message);
  }
}
