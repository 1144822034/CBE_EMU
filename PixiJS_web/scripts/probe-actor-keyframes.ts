import fs from "node:fs";
import { unwrapResource } from "../src/resources/lzss.ts";
import { decodeLkv2Gif } from "../src/resources/lkv2Gif.ts";
import {
  parseActorPayload,
  composeActorPartKeyframe,
  type LoadedActor,
} from "../src/resources/actor.ts";

async function load(name: string): Promise<LoadedActor> {
  const parsed = parseActorPayload(unwrapResource(fs.readFileSync(`../bin/JHOnlineData/${name}`)), name);
  const images = await Promise.all(
    parsed.imageNames.map(async (gif) => {
      try {
        return decodeLkv2Gif(unwrapResource(fs.readFileSync(`../bin/JHOnlineData/${gif}`)));
      } catch {
        return null;
      }
    }),
  );
  return { ...parsed, images };
}

function dumpActor(a: LoadedActor) {
  console.log("====", a.name, "anims", a.animations.length);
  for (let ai = 0; ai < Math.min(a.animations.length, 4); ai++) {
    const n = a.animations[ai]!.parts.length;
    const sizes: string[] = [];
    for (let pi = 0; pi < n; pi++) {
      const f = composeActorPartKeyframe(a, pi, ai);
      sizes.push(f ? `${f.width}x${f.height}` : "null");
    }
    console.log(` anim${ai} keyframes=${n}`, sizes.join(" "));
  }
}

const warriors = await load("hW_warrior.actor");
dumpActor(warriors);
const ghost = await load("e_ghostfireB.actor");
dumpActor(ghost);
const bat = await load("e_batB.actor");
dumpActor(bat);
const npc = await load("FB_NPC.actor");
dumpActor(npc);
