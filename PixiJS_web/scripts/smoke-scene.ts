import { loadSceneMap } from "../src/resources/loader";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

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

const scene = await loadSceneMap("01桃花岛_01.sce");
console.log(
  `scene OK ${scene.width}x${scene.height} map=${scene.mapFile} rgba=${scene.rgba.length}`,
);
