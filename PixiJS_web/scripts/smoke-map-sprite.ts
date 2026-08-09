import fs from "node:fs";
import path from "node:path";
import { unwrapResource } from "../src/resources/lzss.ts";
import { parseSceCombatSpawns } from "../src/resources/sce.ts";

const root = path.resolve("../bin/JHOnlineData");
const scene = "17无情谷_01.sce";
const spawns = parseSceCombatSpawns(
  unwrapResource(fs.readFileSync(path.join(root, scene))),
);
console.log(scene, "n=" + spawns.length);
for (const s of spawns) {
  console.log(
    `  ${s.name} map=${s.mapActorResource} battle=${s.battleActorResource}`,
  );
  if (!/ghostfire/i.test(s.mapActorResource)) {
    console.error("FAIL map should be ghostfire");
    process.exitCode = 1;
  }
  if (!/bee/i.test(s.battleActorResource)) {
    console.error("FAIL battle should be bee");
    process.exitCode = 1;
  }
}
if (!process.exitCode) console.log("OK map=ghostfire battle=bee");
