import { readFileSync } from "node:fs";
import { unwrapResource } from "../src/resources/lzss.ts";
import { parseMapPayload } from "../src/resources/map.ts";
import { parseSce2, parseSceCombatSpawns } from "../src/resources/sce.ts";

const root = "../bin/JHOnlineData";
const names = ["01桃花岛_01.map", "c00蓬莱仙岛_01.map", "00蓬莱仙岛_01.map", "17无情谷_01.map"];

for (const name of names) {
  try {
    const map = parseMapPayload(
      unwrapResource(new Uint8Array(readFileSync(`${root}/${name}`))),
      name,
    );
    const hist = new Map<number, number>();
    for (const c of map.cells) {
      const hi = (c >>> 28) & 0xf;
      hist.set(hi, (hist.get(hi) || 0) + 1);
    }
    console.log(
      name,
      `${map.cols}x${map.rows}`,
      "hiNibble",
      [...hist.entries()].sort((a, b) => a[0] - b[0]),
    );
  } catch (e) {
    console.log(name, (e as Error).message);
  }
}

// Scan SCE for portal-like markers and telestone
for (const sce of ["01桃花岛_01.sce", "00蓬莱仙岛_01.sce", "c00蓬莱仙岛_01.sce"]) {
  try {
    const raw = unwrapResource(new Uint8Array(readFileSync(`${root}/${sce}`)));
    const text = Buffer.from(raw).toString("latin1");
    for (const marker of ["n_telestone", "n_teleporter", ".sce"]) {
      let count = 0;
      let idx = 0;
      while ((idx = text.indexOf(marker, idx)) >= 0) {
        count++;
        idx += marker.length;
      }
      if (marker !== ".sce" || count < 20) console.log(sce, marker, count);
    }
    console.log(sce, "combat", parseSceCombatSpawns(raw).length, "dims", parseSce2(raw).width);
  } catch (e) {
    console.log(sce, (e as Error).message);
  }
}
