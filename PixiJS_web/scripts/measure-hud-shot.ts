/**
 * Measure CBE HUD bands from a 240×400 reference-shot PNG.
 * Finds horizontal rows that are mostly "UI chrome" (low variance / green-grey)
 * vs map (higher green variance).
 */
import fs from "node:fs";
import zlib from "node:zlib";

function readPng(path: string): { w: number; h: number; rgba: Uint8ClampedArray } {
  const buf = fs.readFileSync(path);
  if (buf[0] !== 0x89) throw new Error("not png");
  let pos = 8;
  let w = 0,
    h = 0,
    idat: Buffer[] = [];
  while (pos < buf.length) {
    const len = buf.readUInt32BE(pos);
    const type = buf.toString("ascii", pos + 4, pos + 8);
    const data = buf.subarray(pos + 8, pos + 8 + len);
    if (type === "IHDR") {
      w = data.readUInt32BE(0);
      h = data.readUInt32BE(4);
    } else if (type === "IDAT") idat.push(Buffer.from(data));
    pos += 12 + len;
  }
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const rgba = new Uint8ClampedArray(w * h * 4);
  const stride = w * 4;
  let rp = 0;
  for (let y = 0; y < h; y++) {
    const filter = raw[rp++]!;
    for (let x = 0; x < stride; x++) {
      let v = raw[rp++]!;
      if (filter === 1 && x >= 4) v = (v + rgba[y * stride + (x - 4)]!) & 255;
      else if (filter === 2 && y > 0) v = (v + rgba[(y - 1) * stride + x]!) & 255;
      else if (filter === 3) {
        const a = x >= 4 ? rgba[y * stride + (x - 4)]! : 0;
        const b = y > 0 ? rgba[(y - 1) * stride + x]! : 0;
        v = (v + Math.floor((a + b) / 2)) & 255;
      } else if (filter === 4) {
        const a = x >= 4 ? rgba[y * stride + (x - 4)]! : 0;
        const b = y > 0 ? rgba[(y - 1) * stride + x]! : 0;
        const c = x >= 4 && y > 0 ? rgba[(y - 1) * stride + (x - 4)]! : 0;
        const p = a + b - c;
        const pa = Math.abs(p - a),
          pb = Math.abs(p - b),
          pc = Math.abs(p - c);
        const pr = pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
        v = (v + pr) & 255;
      }
      rgba[y * stride + x] = v;
    }
  }
  return { w, h, rgba };
}

function rowStats(rgba: Uint8ClampedArray, w: number, y: number) {
  let sumR = 0,
    sumG = 0,
    sumB = 0;
  let minL = 255,
    maxL = 0;
  for (let x = 0; x < w; x++) {
    const i = (y * w + x) * 4;
    const r = rgba[i]!,
      g = rgba[i + 1]!,
      b = rgba[i + 2]!;
    sumR += r;
    sumG += g;
    sumB += b;
    const L = (r + g + b) / 3;
    if (L < minL) minL = L;
    if (L > maxL) maxL = L;
  }
  return {
    r: sumR / w,
    g: sumG / w,
    b: sumB / w,
    range: maxL - minL,
  };
}

const path = process.argv[2] || "reference-shots/000017_00025672-game.png";
const { w, h, rgba } = readPng(path);
console.log("size", w, h);

// Print row summaries every row for top 80 and bottom 80
function dumpBand(y0: number, y1: number) {
  for (let y = y0; y < y1; y++) {
    const s = rowStats(rgba, w, y);
    const tag =
      s.range < 25
        ? "FLAT"
        : s.range < 60
          ? "UI?"
          : s.g > s.r + 15 && s.g > s.b
            ? "MAPG"
            : "MIX";
    console.log(
      `y=${String(y).padStart(3)} rng=${s.range.toFixed(0).padStart(3)} rgb=(${s.r.toFixed(0)},${s.g.toFixed(0)},${s.b.toFixed(0)}) ${tag}`,
    );
  }
}

console.log("--- TOP ---");
dumpBand(0, 70);
console.log("--- BOTTOM ---");
dumpBand(h - 70, h);

// Find left-edge of first non-dark pixel clusters on status row ~y30
function scanX(y: number) {
  const runs: { x0: number; x1: number; avg: number }[] = [];
  let x = 0;
  while (x < w) {
    const i = (y * w + x) * 4;
    const L = (rgba[i]! + rgba[i + 1]! + rgba[i + 2]!) / 3;
    if (L < 20) {
      x++;
      continue;
    }
    const x0 = x;
    let sum = 0,
      n = 0;
    while (x < w) {
      const j = (y * w + x) * 4;
      const L2 = (rgba[j]! + rgba[j + 1]! + rgba[j + 2]!) / 3;
      if (L2 < 20) break;
      sum += L2;
      n++;
      x++;
    }
    if (n >= 4) runs.push({ x0, x1: x - 1, avg: sum / n });
  }
  console.log(
    `x-runs y=${y}:`,
    runs.map((r) => `${r.x0}-${r.x1}(L${r.avg.toFixed(0)})`).join(" "),
  );
}
for (const y of [8, 24, 28, 32, 36, 40, 48, 52, 56, 340, 350, 360, 370, 380, 390]) {
  if (y < h) scanX(y);
}
