import fs from "node:fs";
import path from "node:path";
import { unwrapResource } from "../src/resources/lzss.ts";
import { parseActorPayload } from "../src/resources/actor.ts";

const root = path.resolve("../bin/JHOnlineData");
const names = [
  "hW_warrior.actor",
  "h_warrior.actor",
  "hW_mage.actor",
  "e_ghostfireB.actor",
  "e_ghostfireG.actor",
];

// Also find a few e_ and npc actors
const all = fs.readdirSync(root).filter((n) => /\.actor$/i.test(n));
const extra = [
  ...all.filter((n) => /^e_/i.test(n)).slice(0, 4),
  ...all.filter((n) => /^n_/i.test(n) || /huang|npc/i.test(n)).slice(0, 4),
];
for (const n of [...new Set([...names, ...extra])]) {
  const p = path.join(root, n);
  if (!fs.existsSync(p)) {
    console.log(n, "MISSING");
    continue;
  }
  try {
    const actor = parseActorPayload(unwrapResource(fs.readFileSync(p)), n);
    const summary = actor.animations.map((a, i) => {
      const parts = a.parts.map((p) => `${p.partId}:${p.frames.length}`).join(",");
      return `a${i}[${parts}]`;
    });
    console.log(
      n,
      "imgs",
      actor.imageNames.length,
      "rects",
      actor.rects.length,
      "anims",
      actor.animations.length,
      summary.slice(0, 12).join(" "),
      summary.length > 12 ? `...+${summary.length - 12}` : "",
    );
  } catch (e) {
    console.log(n, "ERR", (e as Error).message);
  }
}
