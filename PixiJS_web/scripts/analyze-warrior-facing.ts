import { readFileSync } from "node:fs";
import path from "node:path";
import { PNG } from "pngjs";

const dir = path.resolve("../tmp/facing-probe");

function analyzeWarrior(name: string): void {
  const png = PNG.sync.read(readFileSync(path.join(dir, name)));
  let bodyX = 0;
  let bodyN = 0;
  let bladeMinX = Infinity;
  let bladeMaxX = -Infinity;
  let bladeN = 0;
  for (let y = 0; y < png.height; y++) {
    for (let x = 0; x < png.width; x++) {
      const i = (y * png.width + x) * 4;
      const r = png.data[i]!;
      const g = png.data[i + 1]!;
      const b = png.data[i + 2]!;
      const a = png.data[i + 3]!;
      if (a < 40) continue;
      // red/gold tunic
      if (r > 140 && g < 120 && b < 100) {
        bodyX += x;
        bodyN++;
      }
      // pale blade
      if (r > 180 && g > 180 && b > 180 && y < png.height * 0.55) {
        bladeMinX = Math.min(bladeMinX, x);
        bladeMaxX = Math.max(bladeMaxX, x);
        bladeN++;
      }
    }
  }
  const bx = bodyN ? bodyX / bodyN : NaN;
  const bladeMid = bladeN ? (bladeMinX + bladeMaxX) / 2 : NaN;
  // blade left of body → swinging/facing left side of screen
  const face =
    bladeMid < bx ? "bladeLEFT(=faceL)" : "bladeRIGHT(=faceR)";
  console.log(
    name.padEnd(28),
    "body",
    bx.toFixed(1),
    "blade",
    Number.isFinite(bladeMid) ? bladeMid.toFixed(1) : "n/a",
    "=>",
    face,
  );
}

for (const f of [
  "war_anim6_p4.png",
  "war_anim7_p4.png",
  "battle_war_atk_faceL.png",
  "battle_war_atk_faceR.png",
  "battle_war_idle_faceL.png",
  "war_idle_faceL.png",
  "war_idle_faceR.png",
]) {
  analyzeWarrior(f);
}
