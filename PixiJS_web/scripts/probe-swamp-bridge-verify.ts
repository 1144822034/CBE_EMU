import { readFileSync, readdirSync } from "node:fs";
import { join } from "node:path";
import { unwrapResource } from "../src/resources/lzss.ts";
import {
  parseMapPayload,
  mapCellFlags,
  mapMoveBlocked,
  mapBlocksAt,
} from "../src/resources/map.ts";

/** Regression: 27深渊沼泽_01 bridge strip must be crossable E–W. */
const root = "public/JHOnlineData";
const swamp = readdirSync(root).find(
  (f) => f.startsWith("27") && f.includes("_01") && f.endsWith(".map"),
)!;
const map = parseMapPayload(
  unwrapResource(new Uint8Array(readFileSync(join(root, swamp)))),
  swamp,
);

function center(gx: number, gy: number) {
  return {
    x: gx * map.tileWidth + (map.tileWidth >> 1),
    y: gy * map.tileHeight + (map.tileHeight >> 1),
  };
}

let ok = true;
for (const gy of [21, 22]) {
  for (let gx = 10; gx < 16; gx++) {
    const a = center(gx, gy);
    const b = center(gx + 1, gy);
    if (mapMoveBlocked(map, a.x, a.y, b.x, b.y, 1)) {
      console.log("FAIL bridge step", gy, gx, "->", gx + 1);
      ok = false;
    }
  }
}

const onBridge = center(13, 21);
const intoWater = center(13, 20);
if (!mapBlocksAt(map, intoWater.x, intoWater.y, 0)) {
  console.log("FAIL water not solid");
  ok = false;
}
if (!mapMoveBlocked(map, onBridge.x, onBridge.y, intoWater.x, intoWater.y, 1)) {
  console.log("FAIL should block into water");
  ok = false;
}

console.log(
  "bridge flags row21",
  [11, 12, 13, 14, 15]
    .map((gx) => mapCellFlags(map.cells[gx * map.rows + 21]!).toString(16))
    .join(""),
);
console.log(ok ? "PASS" : "FAIL");
process.exit(ok ? 0 : 1);
