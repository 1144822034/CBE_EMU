import fs from "node:fs";
import { unwrapResource } from "../src/resources/lzss.ts";
import { parseSceCombatSpawns } from "../src/resources/sce.ts";

const tally = new Map<string, number>();
for (const s of fs.readdirSync("../bin/JHOnlineData").filter((f) => /\.sce$/i.test(f))) {
  try {
    for (const sp of parseSceCombatSpawns(
      unwrapResource(fs.readFileSync(`../bin/JHOnlineData/${s}`)),
    )) {
      tally.set(sp.mapActorResource, (tally.get(sp.mapActorResource) || 0) + 1);
    }
  } catch {
    /* skip */
  }
}
const rows = [...tally.entries()].sort((a, b) => b[1] - a[1]);
for (const [k, n] of rows) console.log(n, k);
console.log(
  "non-ghostfire",
  rows.filter(([k]) => !/ghostfire/i.test(k)).map(([k, n]) => `${n}:${k}`).join(", ") || "(none)",
);
