import fs from "node:fs";

const buf = new Uint8Array(fs.readFileSync("../bin/font_gb.uc3"));
const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
const width = dv.getInt32(4, true);
const height = dv.getInt32(8, true);
const count = dv.getInt32(12, true);
const bitmapSize = (width * height) / 8;
const glyphs = new Map<number, Uint8Array>();

function dec(lo: number, hi: number): number | null {
  try {
    return new TextDecoder("gbk").decode(Uint8Array.of(lo, hi)).codePointAt(0) ?? null;
  } catch {
    return null;
  }
}

for (let i = 0; i < count; i++) {
  const gb = dv.getUint16(16 + i * 6, true);
  const off = dv.getUint32(16 + i * 6 + 2, true);
  const bits = buf.subarray(off, off + bitmapSize);
  if ((gb & 0xff) === 0 && gb >= 0x2000 && gb <= 0x7f00) glyphs.set(gb >> 8, bits);
  else {
    const cp = dec(gb & 0xff, (gb >> 8) & 0xff);
    if (cp != null) glyphs.set(cp, bits);
  }
}
console.log("size", width, height, "count", count, "indexed", glyphs.size);
console.log("A", glyphs.has(65), "黄", glyphs.has("黄".codePointAt(0)!), "蓉", glyphs.has("蓉".codePointAt(0)!));
