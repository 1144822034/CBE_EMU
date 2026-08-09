import fs from "node:fs";
import { unwrapResource } from "../src/resources/lzss.ts";
import { parseSceCombatSpawns } from "../src/resources/sce.ts";
import {
  parseActorPayload,
  composePartStacked,
  composeMapActor,
  planMapAnims,
  type LoadedActor,
} from "../src/resources/actor.ts";
import { decodeLkv2Gif } from "../src/resources/lkv2Gif.ts";

function hash(rgba: Uint8ClampedArray): string {
  let h = 2166136261;
  for (let i = 0; i < rgba.length; i += 8) {
    h ^= rgba[i]!;
    h = Math.imul(h, 16777619);
  }
  return (h >>> 0).toString(16);
}

async function load(name: string): Promise<LoadedActor> {
  const parsed = parseActorPayload(
    unwrapResource(fs.readFileSync(`../bin/JHOnlineData/${name}`)),
    name,
  );
  const images = await Promise.all(
    parsed.imageNames.map((gif) => {
      try {
        return decodeLkv2Gif(
          unwrapResource(fs.readFileSync(`../bin/JHOnlineData/${gif}`)),
        );
      } catch {
        return null;
      }
    }),
  );
  return { ...parsed, images };
}

const tally = new Map<string, number>();
for (const s of fs.readdirSync("../bin/JHOnlineData").filter((f) => /\.sce$/i.test(f))) {
  try {
    const spawns = parseSceCombatSpawns(
      unwrapResource(fs.readFileSync(`../bin/JHOnlineData/${s}`)),
    );
    for (const sp of spawns) {
      const k = `${sp.mapActorResource}\t${sp.battleActorResource || sp.sceActorId}`;
      tally.set(k, (tally.get(k) || 0) + 1);
    }
  } catch {
    /* skip */
  }
}
console.log("=== top SCE map|battle ===");
for (const [k, n] of [...tally.entries()].sort((a, b) => b[1] - a[1]).slice(0, 20)) {
  console.log(n, k);
}

const mapActors = [
  ...new Set(
    [...tally.keys()].map((k) => k.split("\t")[0]!).filter((n) => n.endsWith(".actor")),
  ),
];
console.log("\n=== map actor part semantics ===");
for (const name of mapActors.slice(0, 25)) {
  if (!fs.existsSync(`../bin/JHOnlineData/${name}`)) {
    console.log("MISSING", name);
    continue;
  }
  const a = await load(name);
  const plan = planMapAnims(a);
  const anim = a.animations[plan.standAnim]!;
  const hashes = anim.parts.map((p, pi) => {
    const c = composePartStacked(a, plan.standAnim, pi);
    return c ? hash(c.rgba) : "null";
  });
  const uniq = new Set(hashes).size;
  const dirs = [1, 2, 3, 4].map((d) => {
    const c = composeMapActor(a, d, false, 0)!;
    return hash(c.rgba).slice(0, 6);
  });
  const dirUniq = new Set(dirs).size;
  // Idle should flicker for orb-like: check if phase changes part
  const idlePhases = [0, 1, 2, 3].map((ph) => {
    const c = composeMapActor(a, 3, false, ph)!;
    return hash(c.rgba).slice(0, 6);
  });
  const idleFlicker = new Set(idlePhases).size;
  console.log(
    name,
    `parts=${anim.parts.length} uniqParts=${uniq} dirUniq=${dirUniq} idlePhaseUniq=${idleFlicker}`,
    `plan=${plan.mode}/${plan.standAnim}/[${plan.walkAnims}]`,
    uniq === anim.parts.length && dirUniq === uniq
      ? "LIKELY_DIR_OR_LOCKED_FRAMES"
      : uniq < anim.parts.length
        ? "PADDED_DIRS_OR_FLAP"
        : "?",
  );
}
