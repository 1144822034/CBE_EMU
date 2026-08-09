import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(root, "../..");
const candidates = [
  path.join(repo, "web", "fs", "JHOnlineData"),
  path.join(repo, "bin", "JHOnlineData"),
];
const assetRoot = candidates.find((p) => fs.existsSync(p));
if (!assetRoot) {
  console.error("JHOnlineData not found");
  process.exit(1);
}

const scenes = fs
  .readdirSync(assetRoot)
  .filter((n) => n.toLowerCase().endsWith(".sce"))
  .sort((a, b) => a.localeCompare(b, "zh"));

const preferred = [
  "01桃花岛_01.sce",
  "c00蓬莱仙岛_01.sce",
  "11终南山_01.sce",
];
const defaultScene =
  preferred.find((n) => scenes.includes(n)) ?? scenes[0] ?? "";

const outDir = path.join(root, "../public");
fs.mkdirSync(outDir, { recursive: true });
const out = {
  generatedAt: new Date().toISOString(),
  assetRootHint: path.relative(repo, assetRoot).replaceAll("\\", "/"),
  defaultScene,
  scenes,
};
fs.writeFileSync(
  path.join(outDir, "scene-index.json"),
  JSON.stringify(out, null, 2),
  "utf8",
);
console.log(
  `wrote public/scene-index.json scenes=${scenes.length} default=${defaultScene}`,
);
