import fs from "node:fs";
import path from "node:path";
import { deflateSync } from "node:zlib";
import { unwrapResource } from "../src/resources/lzss.ts";
import { decodeLkv2Gif } from "../src/resources/lkv2Gif.ts";
import { readU32LE, decodeGbk } from "../src/resources/binary.ts";

function crc32(buf: Buffer): number {
  let c = ~0;
  for (let i = 0; i < buf.length; i++) {
    c ^= buf[i]!;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
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
    Buffer.concat([
      signature,
      chunk("IHDR", ihdrData),
      chunk("IDAT", compressed),
      chunk("IEND", Buffer.alloc(0)),
    ]),
  );
}

const root = path.resolve("../bin/JHOnlineData");
const out = path.resolve("tmp-ui");
fs.mkdirSync(out, { recursive: true });

for (const name of [
  "ui_timer.gif",
  "UI_menu2.gif",
  "xu1.gif",
  "xu2.gif",
  "xu3.gif",
  "handkuang.gif",
  "tiao.gif",
  "ui_hpsp.gif",
]) {
  try {
    const g = decodeLkv2Gif(unwrapResource(fs.readFileSync(path.join(root, name))));
    writePng(path.join(out, name.replace(/\.gif$/i, ".png")), g.width, g.height, g.rgba);
    console.log(name, `${g.width}x${g.height}`);
  } catch (e) {
    console.log(name, String((e as Error).message || e));
  }
}

const raw = unwrapResource(fs.readFileSync(path.join(root, "eidolon.dsh")));
const cols = readU32LE(raw, 4);
const rows = readU32LE(raw, 8);
console.log("eidolon cols", cols, "rows", rows, "len", raw.length);
let pos = 16;
for (let i = 0; i < cols; i++) {
  const fl = raw[pos++]!;
  console.log(" col", i, decodeGbk(raw.subarray(pos, pos + fl)));
  pos += fl;
}
for (let row = 0; row < Math.min(rows, 25); row++) {
  const rowLen = readU32LE(raw, pos);
  let rp = pos + 4;
  const end = rp + rowLen;
  const cells: string[] = [];
  for (let c = 0; c < cols && rp < end; c++) {
    const vl = raw[rp++]!;
    cells.push(decodeGbk(raw.subarray(rp, rp + vl)));
    rp += vl;
  }
  console.log(" row", cells.join(" | "));
  pos = end;
}
