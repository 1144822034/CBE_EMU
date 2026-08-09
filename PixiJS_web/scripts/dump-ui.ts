import fs from "node:fs";
import path from "node:path";
import { createRequire } from "node:module";
import { unwrapResource } from "../src/resources/lzss.ts";
import { decodeLkv2Gif } from "../src/resources/lkv2Gif.ts";

const root = path.resolve("../bin/JHOnlineData");
const out = path.resolve("tmp-ui");
fs.mkdirSync(out, { recursive: true });

// Minimal PNG writer (no deps): uncompressed RGBA via zlib
import { deflateSync } from "node:zlib";

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
  fs.writeFileSync(file, Buffer.concat([signature, chunk("IHDR", ihdrData), chunk("IDAT", compressed), chunk("IEND", Buffer.alloc(0))]));
}

const names = [
  "UI_menu2.gif",
  "ui_hpsp.gif",
  "ui_offline.gif",
  "ui_selectchar.gif",
  "autobattle_button.gif",
  "autobattle_words.gif",
  "loading.gif",
  "title2.gif",
  "title001.gif",
  "uiexp.gif",
  "UISelect1.gif",
  "UISelect2.gif",
  "ui_bag.gif",
  "ui_quest.gif",
  "ui_chatlog.gif",
  "ui_switch.gif",
  "exp_gold.gif",
  "zhang.gif",
];

for (const name of names) {
  try {
    const g = decodeLkv2Gif(unwrapResource(fs.readFileSync(path.join(root, name))));
    writePng(path.join(out, name.replace(/\.gif$/i, ".png")), g.width, g.height, g.rgba);
    console.log(name, `${g.width}x${g.height}`);
  } catch (e) {
    console.log(name, String((e as Error).message || e));
  }
}
