import fs from "node:fs";
import { unwrapResource } from "../src/resources/lzss.ts";
import { decodeLkv2Gif } from "../src/resources/lkv2Gif.ts";
import {
  parseActorPayload,
  composeMapActor,
  composePartStacked,
  planMapAnims,
  type LoadedActor,
} from "../src/resources/actor.ts";

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

function usesDeath(a: LoadedActor, cbeDir: number): boolean {
  // Recompose isn't enough — check plan doesn't pick anim 8/9
  const plan = planMapAnims(a);
  if (plan.mode !== "player4") return false;
  const c = composeMapActor(a, cbeDir, false, 0);
  if (!c) return true;
  // death poses are short/wide fallen; side body ~23x38
  return c.height >= 40 && c.width <= 24;
}

const war = await load("hW_warrior.actor");
console.log("plan", planMapAnims(war));
const keys: string[] = [];
for (let dir = 1; dir <= 4; dir++) {
  const c = composeMapActor(war, dir, false, 0)!;
  const key = `${c.width}x${c.height}:${hash(c.rgba)}`;
  keys.push(key);
  console.log(`dir${dir}`, key, "deathish?", usesDeath(war, dir));
}
if (new Set(keys).size < 4) {
  console.error("FAIL need 4 unique facings", keys);
  process.exitCode = 1;
}
// Explicit: left/right must not match previous death hashes
const deathish = [1, 2, 3, 4].some((d) => {
  const c = composeMapActor(war, d, false, 0)!;
  return c.height >= 40;
});
if (deathish) {
  console.error("FAIL left/right still look like death (tall thin)");
  process.exitCode = 1;
} else {
  console.log("OK no death-sized stand poses");
}

// Side should be ~23 wide (body without jian trail 37)
const right = composeMapActor(war, 2, false, 0)!;
const left = composeMapActor(war, 4, false, 0)!;
if (right.width > 30 || left.width > 30) {
  console.error("FAIL side still includes sword trail", right.width, left.width);
  process.exitCode = 1;
} else {
  console.log("OK side body widths", right.width, left.width);
}

// CBE map idle: side 收脚 = walk anim part6 (not stand anim4/5 part0, legs apart).
function bodyOnly(a: LoadedActor, ai: number, pi: number) {
  const part = a.animations[ai]!.parts[pi]!;
  const kept = part.frames.filter((f) => {
    const name = a.imageNames[a.rects[f.rectIndex]!.imageIndex] || "";
    return !/jian|guang|death/i.test(name);
  });
  const orig = part.frames;
  (part as { frames: typeof orig }).frames = kept;
  const c = composePartStacked(a, ai, pi);
  part.frames = orig;
  return c;
}
const a0p6 = composePartStacked(war, 0, 6)!;
const a2p6 = composePartStacked(war, 2, 6)!;
if (hash(right.rgba) !== hash(a0p6.rgba) || hash(left.rgba) !== hash(a2p6.rgba)) {
  console.error("FAIL dir2 idle must be anim0 part6, dir4 idle anim2 part6", {
    right: hash(right.rgba),
    a0p6: hash(a0p6.rgba),
    left: hash(left.rgba),
    a2p6: hash(a2p6.rgba),
  });
  process.exitCode = 1;
} else {
  console.log("OK CBE side map idle: dir2=anim0p6, dir4=anim2p6 (收脚)");
}
// Stand anim4/5 part0 remains the legs-apart ready pose (not map idle).
const a4 = bodyOnly(war, 4, 0)!;
const a5 = bodyOnly(war, 5, 0)!;
if (hash(right.rgba) === hash(a5.rgba) || hash(left.rgba) === hash(a4.rgba)) {
  console.error("FAIL side idle still using stand anim4/5 part0");
  process.exitCode = 1;
} else {
  console.log("OK side idle ≠ stand anim4/5 part0");
}

// Front/back must NOT reuse left/right walk anims (0=right walk, 2=left walk).
const frontStand = composeMapActor(war, 3, false, 0)!;
const frontWalk = composeMapActor(war, 3, true, 0)!;
const backStand = composeMapActor(war, 1, false, 0)!;
const backWalk = composeMapActor(war, 1, true, 0)!;
const a0 = composePartStacked(war, 0, 0)!;
const a1 = composePartStacked(war, 1, 0)!;
const a2 = composePartStacked(war, 2, 0)!;
const a3 = composePartStacked(war, 3, 0)!;
if (hash(frontStand.rgba) !== hash(a1.rgba) || hash(frontWalk.rgba) !== hash(a1.rgba)) {
  console.error("FAIL S must use anim1 front, not anim0 right-walk", {
    frontStand: hash(frontStand.rgba),
    frontWalk: hash(frontWalk.rgba),
    a0: hash(a0.rgba),
    a1: hash(a1.rgba),
  });
  process.exitCode = 1;
} else {
  console.log("OK S front uses anim1 (stand+walk)");
}
if (hash(backStand.rgba) !== hash(a3.rgba) || hash(backWalk.rgba) !== hash(a3.rgba)) {
  console.error("FAIL W must use anim3 back, not anim2 left-walk", {
    backStand: hash(backStand.rgba),
    backWalk: hash(backWalk.rgba),
    a2: hash(a2.rgba),
    a3: hash(a3.rgba),
  });
  process.exitCode = 1;
} else {
  console.log("OK W back uses anim3 (stand+walk)");
}
// Right/left walk should use anim0/anim2
const rightWalk = composeMapActor(war, 2, true, 0)!;
const leftWalk = composeMapActor(war, 4, true, 0)!;
if (hash(rightWalk.rgba) !== hash(a0.rgba) || hash(leftWalk.rgba) !== hash(a2.rgba)) {
  console.error("FAIL D walk=anim0 A walk=anim2", {
    rightWalk: hash(rightWalk.rgba),
    a0: hash(a0.rgba),
    leftWalk: hash(leftWalk.rgba),
    a2: hash(a2.rgba),
  });
  process.exitCode = 1;
} else {
  console.log("OK D/A walk use anim0/anim2");
}
