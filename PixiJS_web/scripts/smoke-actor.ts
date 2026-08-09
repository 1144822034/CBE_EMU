import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import {
  composeActorFrameIndex,
  loadActor,
  parseActorPayload,
  pickStandPart,
} from "../src/resources/actor";
import { unwrapResource } from "../src/resources/lzss";

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const assetRoot = [
  path.join(repo, "web", "fs", "JHOnlineData"),
  path.join(repo, "bin", "JHOnlineData"),
].find((p) => fs.existsSync(p))!;

globalThis.fetch = (async (input: RequestInfo | URL) => {
  const url = String(input);
  const marker = "/JHOnlineData/";
  const idx = url.indexOf(marker);
  const name = decodeURIComponent(url.slice(idx + marker.length).split("?")[0]!);
  const buf = fs.readFileSync(path.join(assetRoot, name));
  return {
    ok: true,
    status: 200,
    arrayBuffer: async () =>
      buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength),
  } as Response;
}) as typeof fetch;

const samples = [
  "n_blacksmith.actor",
  "e_monkey.actor",
  "h_warrior.actor",
  "ui_h_war.actor",
];

for (const name of samples) {
  const raw = unwrapResource(new Uint8Array(fs.readFileSync(path.join(assetRoot, name))));
  const parsed = parseActorPayload(raw, name);
  const part = pickStandPart(parsed);
  console.log(
    `${name}: images=${parsed.imageNames.length} rects=${parsed.rects.length} anims=${parsed.animations.length} standFrames=${part?.frames.length ?? 0} gif0=${parsed.imageNames[0]}`,
  );
  const loaded = await loadActor(name);
  const frame = composeActorFrameIndex(loaded, 0);
  if (!frame) throw new Error(`compose failed ${name}`);
  console.log(`  composed ${frame.width}x${frame.height} origin=(${frame.originX},${frame.originY})`);
}

console.log("actor smoke OK");
