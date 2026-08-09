import { readFileSync, readdirSync } from "node:fs";
import path from "node:path";
import { PNG } from "pngjs";

const dir = path.resolve("../tmp/facing-probe");

function analyze(name: string): void {
  const png = PNG.sync.read(readFileSync(path.join(dir, name)));
  let headX = 0;
  let headN = 0;
  let bodyX = 0;
  let bodyN = 0;
  for (let y = 0; y < png.height; y++) {
    for (let x = 0; x < png.width; x++) {
      const i = (y * png.width + x) * 4;
      const r = png.data[i]!;
      const g = png.data[i + 1]!;
      const b = png.data[i + 2]!;
      const a = png.data[i + 3]!;
      if (a < 40) continue;
      if (r + g + b < 120) {
        headX += x;
        headN++;
      } else if (r > 140 && g > 100 && b < 80) {
        bodyX += x;
        bodyN++;
      }
    }
  }
  const hx = headN ? headX / headN : NaN;
  const bx = bodyN ? bodyX / bodyN : NaN;
  const face = hx < bx ? "LEFT" : "RIGHT";
  console.log(
    name.padEnd(28),
    "head",
    hx.toFixed(1),
    "body",
    bx.toFixed(1),
    "=>",
    face,
    `w=${png.width}`,
  );
}

for (const f of readdirSync(dir).filter((x) => x.endsWith(".png")).sort()) {
  analyze(f);
}
