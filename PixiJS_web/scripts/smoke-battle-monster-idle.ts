/**
 * CBE-aligned battle monster idle (掘地鼠口径推广):
 * - e_ratB: body+head fixed, only trailing detail (尾) flips; not anim1
 * - e_stoneJY / e_rockcow: stand detail flap; not anim0↔anim1 walk
 * - e_kirin / e_zhuzongyao: idle must stay on standAnim (no anim1 body morph)
 * - e_beesY: offset-twin flap (same rects, part2); not remapping walk
 * - e_skeletonH: idle must not show wuqi1 slash
 */
import fs from "node:fs";
import { unwrapResource } from "../src/resources/lzss.ts";
import {
  parseActorPayload,
  composeBattlePose,
  composePartStacked,
  planMapAnims,
  type LoadedActor,
  type ActorPart,
} from "../src/resources/actor.ts";
import { decodeLkv2Gif } from "../src/resources/lkv2Gif.ts";

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

function hash(rgba: Uint8ClampedArray): string {
  let h = 2166136261;
  for (let i = 0; i < rgba.length; i += 8) {
    h ^= rgba[i]!;
    h = Math.imul(h, 16777619);
  }
  return (h >>> 0).toString(16);
}

function animHasImage(a: LoadedActor, ai: number, re: RegExp): boolean {
  for (const part of a.animations[ai]?.parts ?? []) {
    for (const f of part.frames) {
      const rect = a.rects[f.rectIndex];
      if (!rect) continue;
      if (re.test(a.imageNames[rect.imageIndex] ?? "")) return true;
    }
  }
  return false;
}

function bodyHeadKey(part: ActorPart): string {
  return part.frames
    .slice(0, -1)
    .map((f) => `${f.rectIndex}:${f.offsetX}:${f.offsetY}`)
    .join("|");
}

function idleHashes(a: LoadedActor): string[] {
  return [0, 1, 2, 3, 4, 5, 6, 7].map((ph) =>
    hash(composeBattlePose(a, true, "idle", ph)!.rgba),
  );
}

function standHashes(a: LoadedActor): string[] {
  const plan = planMapAnims(a);
  return (a.animations[plan.standAnim]?.parts ?? []).map((_, pi) =>
    hash(composePartStacked(a, plan.standAnim, pi)!.rgba),
  );
}

function anim1Hashes(a: LoadedActor): string[] {
  if (!a.animations[1]) return [];
  return a.animations[1].parts.map((_, pi) =>
    hash(composePartStacked(a, 1, pi)!.rgba),
  );
}

let failed = false;

{
  const a = await load("e_ratB.actor");
  const stand = a.animations[0]!;
  const partHash = stand.parts.map((p, pi) =>
    hash(composePartStacked(a, 0, pi)!.rgba),
  );
  const idle = idleHashes(a);
  const idleParts: ActorPart[] = [];
  for (const h of idle) {
    const pi = partHash.indexOf(h);
    if (pi >= 0) idleParts.push(stand.parts[pi]!);
  }
  if (new Set(idle).size < 2) {
    console.error("FAIL e_ratB idle frozen");
    failed = true;
  }
  if (new Set(idleParts.map(bodyHeadKey)).size !== 1) {
    console.error("FAIL e_ratB idle moved body/head");
    failed = true;
  }
  if (!failed) console.log("OK e_ratB idleTailWag");
}

for (const name of ["e_stoneJY.actor", "e_rockcow.actor"]) {
  const a = await load(name);
  const idle = idleHashes(a);
  const standH = standHashes(a);
  const a1 = anim1Hashes(a);
  if (new Set(idle).size < 2) {
    console.error("FAIL", name, "idle frozen");
    failed = true;
    continue;
  }
  if (!idle.every((h) => standH.includes(h))) {
    console.error("FAIL", name, "idle left standAnim");
    failed = true;
    continue;
  }
  // Prefer not to need anim1-only frames for idle.
  if (idle.some((h) => a1.includes(h) && !standH.includes(h))) {
    console.error("FAIL", name, "idle used anim1-only frame");
    failed = true;
    continue;
  }
  console.log("OK", name, "standDetailFlap uniq", new Set(idle).size);
}

for (const name of ["e_kirin.actor", "e_zhuzongyao.actor", "e_tiger.actor"]) {
  const a = await load(name);
  const idle = idleHashes(a);
  const standH = standHashes(a);
  const a1 = anim1Hashes(a);
  if (new Set(idle).size < 2) {
    console.error("FAIL", name, "idle frozen");
    failed = true;
    continue;
  }
  if (!idle.every((h) => standH.includes(h))) {
    console.error("FAIL", name, "idle left stand", idle);
    failed = true;
    continue;
  }
  if (idle.some((h) => a1.includes(h) && !standH.includes(h))) {
    console.error("FAIL", name, "idle still morphs via anim1");
    failed = true;
    continue;
  }
  console.log("OK", name, "standBreath uniq", new Set(idle).size);
}

for (const name of ["e_beesY.actor", "e_beeG.actor"]) {
  const a = await load(name);
  const idle = idleHashes(a);
  if (new Set(idle).size < 2) {
    console.error("FAIL", name, "offset flap frozen");
    failed = true;
  } else console.log("OK", name, "offsetTwinFlap uniq", new Set(idle).size);
}

{
  const a = await load("e_skeletonH.actor");
  const plan = planMapAnims(a);
  const slashH = hash(composeBattlePose(a, true, "attack", 3)!.rgba);
  const idle = idleHashes(a);
  if (
    plan.walkAnims.some((ai) => animHasImage(a, ai, /wuqi1/i)) &&
    !animHasImage(a, plan.standAnim, /wuqi1/i) &&
    idle.includes(slashH)
  ) {
    console.error("FAIL e_skeletonH idle slash");
    failed = true;
  } else console.log("OK e_skeletonH idleNoSlash");
}

for (const name of [
  "e_believerH.actor",
  "e_soliderH.actor",
  "e_believerK.actor",
  "e_craw.actor",
  "e_monkey.actor",
]) {
  const a = await load(name);
  const idle = idleHashes(a);
  const standH = standHashes(a);
  const a1 = anim1Hashes(a);
  if (new Set(idle).size < 2) {
    console.error("FAIL", name, "idle frozen (need breath bob)");
    failed = true;
    continue;
  }
  if (!idle.every((h) => standH.includes(h))) {
    console.error("FAIL", name, "idle left standAnim");
    failed = true;
    continue;
  }
  if (idle.some((h) => a1.includes(h) && !standH.includes(h))) {
    console.error("FAIL", name, "idle used anim1 morph");
    failed = true;
    continue;
  }
  console.log("OK", name, "standBreathBob uniq", new Set(idle).size);
}

if (failed) process.exit(1);
console.log("OK battle monster idle (stand-detail / bee)");
