import fs from "node:fs";
import { unwrapResource } from "../src/resources/lzss.ts";
import { decodeLkv2Gif } from "../src/resources/lkv2Gif.ts";
import {
  parseActorPayload,
  composeMapActor,
  planMapAnims,
  type LoadedActor,
} from "../src/resources/actor.ts";

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

let failed = false;
const fire = await load("e_ghostfireR.actor");
const plan = planMapAnims(fire);
console.log("ghostfire plan", plan);
if (plan.partMode !== "frames") {
  console.error("FAIL ghostfire should be frames mode");
  failed = true;
}

const idle = [0, 1, 2, 3].map((ph) => hash(composeMapActor(fire, 3, false, ph)!.rgba));
const uniqIdle = new Set(idle).size;
console.log("idle phases", idle.map((h) => h.slice(0, 6)).join(","), "uniq", uniqIdle);
if (uniqIdle < 4) {
  console.error("FAIL ghostfire idle must flicker through 4 parts");
  failed = true;
} else {
  console.log("OK ghostfire flickers on phase");
}

// Facing must not lock a single frame forever
const dirs = [1, 2, 3, 4].map((d) => hash(composeMapActor(fire, d, false, 0)!.rgba));
if (new Set(dirs).size !== 1) {
  // phase 0 → part 0 for all dirs in frames mode
  console.error("FAIL frames mode should ignore dir at same phase", dirs);
  failed = true;
} else {
  console.log("OK ghostfire ignores dir (same phase → same frame)");
}

const bee = await load("e_beesY.actor");
const beePlan = planMapAnims(bee);
console.log("beesY plan", beePlan);
if (beePlan.partMode !== "dirFlap") {
  console.error("FAIL bees should be dirFlap");
  failed = true;
}

const beeG = await load("e_beeG.actor");
const beeGPlan = planMapAnims(beeG);
console.log("beeG plan", beeGPlan);
if (beeGPlan.walkAnims.length > 1) {
  console.error("FAIL beeG a1 is tiny — must not alternate walk anims", beeGPlan.walkAnims);
  failed = true;
} else {
  console.log("OK beeG walkAnims stays on anim0");
}

// player regression
const war = await load("hW_warrior.actor");
const wplan = planMapAnims(war);
if (wplan.mode !== "player4") {
  console.error("FAIL warrior still player4");
  failed = true;
}

if (failed) process.exitCode = 1;
else console.log("OK monster sprite mapping");
