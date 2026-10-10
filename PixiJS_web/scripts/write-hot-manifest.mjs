/**
 * Write dist/hot-manifest.json (relative path → sha256).
 * Android startup compares this file at the hot-update origin and downloads
 * only paths whose hash differs from the APK copy.
 *
 * Also writes PixiJS_web/dist-hot-changed/ with only paths whose content hash
 * changed since the last build (plus the new hot-manifest.json), for uploading
 * to the hot-update server. Baseline: PixiJS_web/.hot-last-manifest.json.
 */
import { createHash } from "node:crypto";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const MANIFEST_NAME = "hot-manifest.json";
const LAST_MANIFEST_NAME = ".hot-last-manifest.json";
const DELTA_DIR_NAME = "dist-hot-changed";
const CHANGED_LIST = "_changed.txt";
const REMOVED_LIST = "_removed.txt";
const REMOVED_BAT = "_removed.bat";
const NOTE_FILE = "_NOTE.txt";

/** Escape a relative path for use inside a double-quoted cmd.exe argument. */
function batQuotePath(relPosix) {
  const win = relPosix.split("/").join("\\");
  return win.replace(/%/g, "%%").replace(/"/g, "");
}

/**
 * Bat to run on the hot-update server root (after merging dist-hot-changed).
 * Uses %~dp0 so it deletes relative to the folder that contains this script.
 * @param {string[]} removed
 */
function buildRemovedBat(removed) {
  const lines = [
    "@echo off",
    "setlocal EnableExtensions",
    "chcp 65001 >nul",
    'cd /d "%~dp0"',
    "echo Deleting obsolete hot-update files...",
  ];
  for (const rel of removed) {
    const q = batQuotePath(rel);
    lines.push(`if exist "${q}" del /f /q "${q}"`);
  }
  lines.push(`echo Done. Removed up to ${removed.length} file(s).`);
  lines.push("endlocal");
  lines.push("");
  // CRLF for Windows servers / Explorer double-click.
  return lines.join("\r\n");
}

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

function readPrevFiles(prevPath) {
  if (!fs.existsSync(prevPath)) return null;
  try {
    const parsed = JSON.parse(fs.readFileSync(prevPath, "utf8"));
    if (!parsed || typeof parsed.files !== "object" || parsed.files == null) {
      return null;
    }
    /** @type {Record<string, string>} */
    const out = {};
    for (const [k, v] of Object.entries(parsed.files)) {
      if (typeof v === "string" && v.length === 64) out[k] = v.toLowerCase();
    }
    return out;
  } catch {
    return null;
  }
}

function rmrf(dir) {
  if (fs.existsSync(dir)) fs.rmSync(dir, { recursive: true, force: true });
}

/**
 * @param {string} distRoot
 * @param {Record<string, string>} files
 * @param {string} pixiRoot
 * @param {{ writeDelta?: boolean }} [opts]
 * @returns {{ changed: string[], removed: string[], baseline: boolean }}
 */
function writeChangedBundle(distRoot, files, pixiRoot, opts = {}) {
  const writeDelta = opts.writeDelta !== false;
  const prevPath = path.join(pixiRoot, LAST_MANIFEST_NAME);
  const deltaDir = path.join(pixiRoot, DELTA_DIR_NAME);
  const prevFiles = readPrevFiles(prevPath);

  /** @type {string[]} */
  const changed = [];
  /** @type {string[]} */
  const removed = [];

  if (prevFiles) {
    for (const [rel, hash] of Object.entries(files)) {
      if (prevFiles[rel] !== hash) changed.push(rel);
    }
    for (const rel of Object.keys(prevFiles)) {
      if (!(rel in files)) removed.push(rel);
    }
    changed.sort();
    removed.sort();
  }

  if (writeDelta) {
    rmrf(deltaDir);
    fs.mkdirSync(deltaDir, { recursive: true });

    if (!prevFiles) {
      fs.writeFileSync(
        path.join(deltaDir, NOTE_FILE),
        [
          "No previous baseline (.hot-last-manifest.json).",
          "This build only saved a baseline; no file copies were staged.",
          "Next build will put content-changed paths here for server upload.",
          "Always upload hot-manifest.json from dist/ (copied below) after each build.",
          "",
        ].join("\n"),
        "utf8",
      );
    } else {
      for (const rel of changed) {
        const from = path.join(distRoot, rel);
        const to = path.join(deltaDir, rel);
        fs.mkdirSync(path.dirname(to), { recursive: true });
        fs.copyFileSync(from, to);
      }
      fs.writeFileSync(
        path.join(deltaDir, CHANGED_LIST),
        changed.length ? `${changed.join("\n")}\n` : "",
        "utf8",
      );
      if (removed.length) {
        fs.writeFileSync(
          path.join(deltaDir, REMOVED_LIST),
          `${removed.join("\n")}\n`,
          "utf8",
        );
        fs.writeFileSync(
          path.join(deltaDir, REMOVED_BAT),
          buildRemovedBat(removed),
          "utf8",
        );
      }
      fs.writeFileSync(
        path.join(deltaDir, NOTE_FILE),
        [
          "Copy this folder onto the hot-update server root (merge).",
          `Changed files: ${changed.length}`,
          `Removed on this build (delete on server if desired): ${removed.length}`,
          removed.length
            ? `Then run _removed.bat on the server root (same folder as hot-manifest.json).`
            : null,
          "hot-manifest.json is always included and must be uploaded.",
          "",
        ]
          .filter((line) => line != null)
          .join("\n"),
        "utf8",
      );
    }

    fs.copyFileSync(
      path.join(distRoot, MANIFEST_NAME),
      path.join(deltaDir, MANIFEST_NAME),
    );
  }

  fs.writeFileSync(prevPath, JSON.stringify({ files }), "utf8");

  return {
    changed: prevFiles ? changed : Object.keys(files).sort(),
    removed,
    baseline: !prevFiles,
  };
}

/**
 * @param {string} distDir
 * @param {{ pixiRoot?: string, writeDelta?: boolean }} [options]
 * @returns {Promise<{ count: number, changed: number, removed: number, baseline: boolean }>}
 */
export async function writeHotManifest(distDir, options = {}) {
  const root = path.resolve(distDir);
  if (!fs.existsSync(root) || !fs.statSync(root).isDirectory()) {
    throw new Error(`dist dir missing: ${root}`);
  }
  const pixiRoot = path.resolve(options.pixiRoot ?? path.dirname(root));
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

  const delta = writeChangedBundle(root, files, pixiRoot, {
    writeDelta: options.writeDelta,
  });

  return {
    count: rels.length,
    changed: delta.baseline ? 0 : delta.changed.length,
    removed: delta.removed.length,
    baseline: delta.baseline,
  };
}

async function selfTest() {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "hot-manifest-test-"));
  const pixi = fs.mkdtempSync(path.join(os.tmpdir(), "hot-manifest-pixi-"));
  try {
    const nested = path.join(dir, "assets");
    fs.mkdirSync(nested);
    fs.writeFileSync(path.join(dir, "index.html"), "<html></html>", "utf8");
    fs.writeFileSync(path.join(nested, "a.js"), "console.log(1)\n", "utf8");
    fs.writeFileSync(path.join(dir, MANIFEST_NAME), "stale", "utf8");

    const first = await writeHotManifest(dir, { pixiRoot: pixi });
    if (first.count !== 2) throw new Error(`expected 2 files, got ${first.count}`);
    if (!first.baseline) throw new Error("first build should establish baseline");
    if (first.changed !== 0) throw new Error("baseline build stages no changed copies");
    const parsed = JSON.parse(fs.readFileSync(path.join(dir, MANIFEST_NAME), "utf8"));
    const expectHtml = createHash("sha256").update("<html></html>").digest("hex");
    const expectJs = createHash("sha256").update("console.log(1)\n").digest("hex");
    if (parsed.files["index.html"] !== expectHtml) throw new Error("index.html hash");
    if (parsed.files["assets/a.js"] !== expectJs) throw new Error("assets/a.js hash");
    if (parsed.files[MANIFEST_NAME]) throw new Error("manifest must not list itself");
    if (!fs.existsSync(path.join(pixi, LAST_MANIFEST_NAME))) {
      throw new Error("missing last-manifest baseline");
    }
    const delta1 = path.join(pixi, DELTA_DIR_NAME);
    if (!fs.existsSync(path.join(delta1, MANIFEST_NAME))) {
      throw new Error("baseline delta must still include hot-manifest.json");
    }
    if (fs.existsSync(path.join(delta1, "index.html"))) {
      throw new Error("baseline must not copy all dist files");
    }

    fs.writeFileSync(path.join(nested, "a.js"), "console.log(2)\n", "utf8");
    fs.writeFileSync(path.join(dir, "only-new.txt"), "x\n", "utf8");
    const second = await writeHotManifest(dir, { pixiRoot: pixi });
    if (second.baseline) throw new Error("second build should use baseline");
    if (second.changed !== 2) throw new Error(`expected 2 changed, got ${second.changed}`);
    const delta2 = path.join(pixi, DELTA_DIR_NAME);
    if (!fs.existsSync(path.join(delta2, "assets/a.js"))) {
      throw new Error("changed assets/a.js missing from delta");
    }
    if (!fs.existsSync(path.join(delta2, "only-new.txt"))) {
      throw new Error("new only-new.txt missing from delta");
    }
    if (fs.existsSync(path.join(delta2, "index.html"))) {
      throw new Error("unchanged index.html must not be in delta");
    }
    const list = fs.readFileSync(path.join(delta2, CHANGED_LIST), "utf8");
    if (!list.includes("assets/a.js") || !list.includes("only-new.txt")) {
      throw new Error("_changed.txt incomplete");
    }

    fs.unlinkSync(path.join(dir, "only-new.txt"));
    const third = await writeHotManifest(dir, { pixiRoot: pixi });
    if (third.removed !== 1) throw new Error(`expected 1 removed, got ${third.removed}`);
    const delta3 = path.join(pixi, DELTA_DIR_NAME);
    const removedList = fs.readFileSync(path.join(delta3, REMOVED_LIST), "utf8");
    if (!removedList.includes("only-new.txt")) throw new Error("_removed.txt missing path");
    const bat = fs.readFileSync(path.join(delta3, REMOVED_BAT), "utf8");
    if (!bat.includes('del /f /q "only-new.txt"')) throw new Error("_removed.bat missing del");
    if (!bat.includes("\r\n")) throw new Error("_removed.bat should use CRLF");

    console.log("hot-manifest self-test ok");
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
    fs.rmSync(pixi, { recursive: true, force: true });
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
      .then((r) => {
        if (r.baseline) {
          console.log(
            `wrote ${MANIFEST_NAME} (${r.count} files); baseline saved → next build fills ${DELTA_DIR_NAME}/`,
          );
        } else {
          console.log(
            `wrote ${MANIFEST_NAME} (${r.count} files); ${r.changed} changed, ${r.removed} removed → ${DELTA_DIR_NAME}/`,
          );
        }
      })
      .catch((err) => {
        console.error(err);
        process.exit(1);
      });
  }
}
