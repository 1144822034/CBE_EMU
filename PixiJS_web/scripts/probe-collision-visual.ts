import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { PNG } from "pngjs";
import { unwrapResource } from "../src/resources/lzss.ts";
import { parseMapPayload } from "../src/resources/map.ts";
import { parseSce2 } from "../src/resources/sce.ts";
import { asciiAt, decodeGbk, readU16LE } from "../src/resources/binary.ts";

const root = "../bin/JHOnlineData";
const out = "../tmp/facing-probe";
mkdirSync(out, { recursive: true });

function dumpHi(name: string): void {
  const map = parseMapPayload(
    unwrapResource(new Uint8Array(readFileSync(`${root}/${name}`))),
    name,
  );
  const png = new PNG({ width: map.cols, height: map.rows });
  for (let index = 0; index < map.cells.length; index++) {
    const c = map.cells[index]!;
    const hi = (c >>> 28) & 0xf;
    const gridX = (index / map.rows) | 0;
    const gridY = index % map.rows;
    const i = (gridY * map.cols + gridX) * 4;
    // hi=0 green walk?, hi=15 red block?, other yellow
    if (hi === 0) {
      png.data[i] = 40;
      png.data[i + 1] = 180;
      png.data[i + 2] = 40;
      png.data[i + 3] = 255;
    } else if (hi === 15) {
      png.data[i] = 200;
      png.data[i + 1] = 40;
      png.data[i + 2] = 40;
      png.data[i + 3] = 255;
    } else {
      png.data[i] = 220;
      png.data[i + 1] = 180;
      png.data[i + 2] = 40;
      png.data[i + 3] = 255;
    }
  }
  writeFileSync(`${out}/coll_${name}.png`, PNG.sync.write(png));
  console.log("wrote", name, map.cols, map.rows);
}

dumpHi("01桃花岛_01.map");
dumpHi("17无情谷_01.map");

// Find scenes with telestone
import { readdirSync } from "node:fs";
let found = 0;
for (const f of readdirSync(root)) {
  if (!f.toLowerCase().endsWith(".sce")) continue;
  try {
    const raw = unwrapResource(new Uint8Array(readFileSync(`${root}/${f}`)));
    const latin = Buffer.from(raw).toString("latin1");
    if (latin.includes("n_telestone") || latin.includes("n_teleporter")) {
      console.log("stone", f);
      found++;
      if (found > 12) break;
    }
  } catch {
    /* skip */
  }
}
console.log("stone scenes listed", found);
