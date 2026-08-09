import fs from "node:fs";
import { unwrapResource } from "../src/resources/lzss.ts";
import { decodeLkv2Gif } from "../src/resources/lkv2Gif.ts";
import {
  parseActorPayload,
  composePartStacked,
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

for (const name of [
  "h_warrior.actor",
  "hW_assassin.actor",
  "h_mage.actor",
  "hW_mage.actor",
]) {
  const a = await load(name);
  console.log(
    name,
    "anims",
    a.animations.length,
    "parts",
    a.animations
      .slice(0, 4)
      .map((x) => x.parts.length)
      .join(","),
  );
  for (const ai of [1, 3, 8, 9]) {
    if (!a.animations[ai]) continue;
    const c = composePartStacked(a, ai, 0);
    let sx = 0;
    let n = 0;
    if (c) {
      for (let y = 0; y < c.height; y++) {
        for (let x = 0; x < c.width; x++) {
          if (c.rgba[(y * c.width + x) * 4 + 3]! > 40) {
            sx += x;
            n++;
          }
        }
      }
    }
    console.log(
      "  a" + ai,
      c ? c.width + "x" + c.height : "null",
      "cx",
      n ? (sx / n).toFixed(1) : "-",
      "mid",
      c ? (c.width / 2).toFixed(1) : "-",
    );
  }
}
