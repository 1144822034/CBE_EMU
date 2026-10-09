/**
 * Write dist/hot-manifest.json (relative path → sha256).
 * Android startup compares this file at the hot-update origin and downloads
 * only paths whose hash differs from the APK copy.
 */
import { createHash } from "node:crypto";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const MANIFEST_NAME = "hot-manifest.json";

function walkFiles(dir, root, out) {
  for (const name of fs.readdirSync(dir)) {
    const abs = path.join(dir, name);
    const rel = path.relative(root, abs).split(path.sep).join("/");
    const st = fs.statSync(abs);
    if (st.isDirectory()) {
      walkFiles(abs, root, out);
      continue;
    }
    if (!st.isFile() || rel === MANIFEST_NAME) continue;
    if (rel === "jni-hot" || rel.startsWith("jni-hot/")) continue;
    out.push(rel);
  }
}

function sha256File(filePath) {
  return new Promise((resolve, reject) => {
    const hash = createHash("sha256");
    const input = fs.createReadStream(filePath);
    input.on("error", reject);
    hash.on("error", reject);
    input.pipe(hash);
    hash.on("finish", () => resolve(hash.digest("hex")));
  });
}

/**
 * @param {string} distDir
 * @returns {Promise<number>} file count written into the manifest
 */
export async function writeHotManifest(distDir) {
  const root = path.resolve(distDir);
  if (!fs.existsSync(root) || !fs.statSync(root).isDirectory()) {
    throw new Error(`dist dir missing: ${root}`);
  }
  const rels = [];
  walkFiles(root, root, rels);
  rels.sort();
  /** @type {Record<string, string>} */
  const files = {};
  for (const rel of rels) {
    files[rel] = await sha256File(path.join(root, rel));
  }
  const body = JSON.stringify({ files });
  fs.writeFileSync(path.join(root, MANIFEST_NAME), body, "utf8");
  return rels.length;
}

async function selfTest() {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "hot-manifest-test-"));
  try {
    const nested = path.join(dir, "assets");
    fs.mkdirSync(nested);
    fs.writeFileSync(path.join(dir, "index.html"), "<html></html>", "utf8");
    fs.writeFileSync(path.join(nested, "a.js"), "console.log(1)\n", "utf8");
    fs.writeFileSync(path.join(dir, MANIFEST_NAME), "stale", "utf8");
    const count = await writeHotManifest(dir);
    if (count !== 2) throw new Error(`expected 2 files, got ${count}`);
    const parsed = JSON.parse(fs.readFileSync(path.join(dir, MANIFEST_NAME), "utf8"));
    const expectHtml = createHash("sha256").update("<html></html>").digest("hex");
    const expectJs = createHash("sha256").update("console.log(1)\n").digest("hex");
    if (parsed.files["index.html"] !== expectHtml) throw new Error("index.html hash");
    if (parsed.files["assets/a.js"] !== expectJs) throw new Error("assets/a.js hash");
    if (parsed.files[MANIFEST_NAME]) throw new Error("manifest must not list itself");
    console.log("hot-manifest self-test ok");
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
}

const invoked =
  process.argv[1] != null && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href;

if (invoked) {
  if (process.argv.includes("--self-test")) {
    selfTest().catch((err) => {
      console.error(err);
      process.exit(1);
    });
  } else {
    const dist = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../dist");
    writeHotManifest(dist)
      .then((count) => {
        console.log(`wrote ${MANIFEST_NAME} (${count} files)`);
      })
      .catch((err) => {
        console.error(err);
        process.exit(1);
      });
  }
}
