import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import path from "node:path";
import { PNG } from "pngjs";
import {
  parseActorPayload,
  composeBattlePose,
  composePartStacked,
  planMapAnims,
  type ComposedFrame,
  type LoadedActor,
} from "../src/resources/actor.ts";
import { unwrapResource } from "../src/resources/lzss.ts";
import { decodeLkv2Gif } from "../src/resources/lkv2Gif.ts";

const root = path.resolve("../bin/JHOnlineData");
const out = path.resolve("../tmp/facing-probe");

async function loadFull(name: string): Promise<LoadedActor> {
  const parsed = parseActorPayload(
    unwrapResource(new Uint8Array(readFileSync(path.join(root, name)))),
    name,
  );
  const images = [];
  for (const img of parsed.imageNames) {
    try {
      images.push(
        decodeLkv2Gif(
          unwrapResource(new Uint8Array(readFileSync(path.join(root, img)))),
        ),
      );
    } catch {
      images.push(null);
    }
  }
  return { ...parsed, images, name };
}

function save(fr: ComposedFrame | null, file: string): void {
  if (!fr) return;
  const png = new PNG({ width: fr.width, height: fr.height });
  const rgba = fr.rgba;
  if (!fr.flipX) {
    png.data.set(rgba);
  } else {
    for (let y = 0; y < fr.height; y++) {
      for (let x = 0; x < fr.width; x++) {
        const si = (y * fr.width + x) * 4;
        const di = (y * fr.width + (fr.width - 1 - x)) * 4;
        png.data[di] = rgba[si]!;
        png.data[di + 1] = rgba[si + 1]!;
        png.data[di + 2] = rgba[si + 2]!;
        png.data[di + 3] = rgba[si + 3]!;
      }
    }
  }
  writeFileSync(file, PNG.sync.write(png));
}

mkdirSync(out, { recursive: true });
const bee = await loadFull("e_beesY.actor");
const war = await loadFull("hW_warrior.actor");
console.log("bee plan", planMapAnims(bee));
for (const p of [0, 2]) {
  for (const a of [0, 1]) {
    save(composePartStacked(bee, a, p), path.join(out, `bee_a${a}p${p}.png`));
  }
}
save(composeBattlePose(bee, true, "idle", 0), path.join(out, "battle_bee_faceR_p0.png"));
save(composeBattlePose(bee, true, "idle", 1), path.join(out, "battle_bee_faceR_p1.png"));
save(composeBattlePose(war, false, "idle", 0), path.join(out, "battle_war_idle_faceL.png"));
save(composeBattlePose(war, false, "attack", 4), path.join(out, "battle_war_atk_faceL.png"));
save(composeBattlePose(war, true, "attack", 4), path.join(out, "battle_war_atk_faceR.png"));
save(composePartStacked(war, 6, 4), path.join(out, "war_anim6_p4.png"));
save(composePartStacked(war, 7, 4), path.join(out, "war_anim7_p4.png"));
console.log("wrote", out);
