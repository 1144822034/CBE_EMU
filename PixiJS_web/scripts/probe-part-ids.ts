import fs from "node:fs";
import { unwrapResource } from "../src/resources/lzss.ts";
import { parseActorPayload } from "../src/resources/actor.ts";

const a = parseActorPayload(
  unwrapResource(fs.readFileSync("../bin/JHOnlineData/hW_warrior.actor")),
  "hW_warrior.actor",
);

for (let ai = 0; ai < a.animations.length; ai++) {
  const parts = a.animations[ai]!.parts;
  console.log(
    `anim${ai}`,
    parts
      .map((p) => `id${p.partId}:f${p.frames.length}`)
      .join(" "),
  );
}

// Compare male warrior / assassin partIds
for (const name of ["h_warrior.actor", "hW_assassin.actor", "e_ghostfireR.actor", "e_beesY.actor"]) {
  try {
    const x = parseActorPayload(
      unwrapResource(fs.readFileSync(`../bin/JHOnlineData/${name}`)),
      name,
    );
    console.log(
      "\n",
      name,
      "anims",
      x.animations.length,
      "a0 parts",
      x.animations[0]?.parts.map((p) => p.partId).join(","),
      "a1 parts",
      x.animations[1]?.parts.map((p) => p.partId).join(","),
    );
  } catch (e) {
    console.log(name, e);
  }
}
