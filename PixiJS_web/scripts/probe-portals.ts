import { readFileSync } from "node:fs";
import { unwrapResource } from "../src/resources/lzss.ts";
import {
  parseScePortals,
  parseSceTeleportStones,
} from "../src/resources/sce.ts";

const root = "../bin/JHOnlineData";
for (const sce of ["01桃花岛_01.sce", "01桃花岛_04.sce", "11终南山_02.sce"]) {
  try {
    const raw = unwrapResource(new Uint8Array(readFileSync(`${root}/${sce}`)));
    const portals = parseScePortals(raw);
    const stones = parseSceTeleportStones(raw);
    console.log(
      sce,
      "portals",
      portals.length,
      "stones",
      stones.map((s) => `${s.name}@${s.x},${s.y}/${s.actorResource}`),
    );
  } catch (e) {
    console.log(sce, (e as Error).message);
  }
}
