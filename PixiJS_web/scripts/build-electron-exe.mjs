/**
 * Build portrait Windows portable exe (electron-builder).
 * Packs into a fresh TEMP dir (avoids Windows EPERM / AV locks on
 * in-tree `electron-release/win-unpacked`), then copies artifacts back.
 *
 *   node scripts/build-electron-exe.mjs
 *   npm run build:exe
 */
import { spawnSync } from "node:child_process";
import {
  copyFileSync,
  cpSync,
  existsSync,
  mkdirSync,
  mkdtempSync,
  readdirSync,
  rmSync,
  statSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { basename, dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const repoRoot = join(root, "..");
const outDir = join(root, "electron-release");

function isJianghuOlCbe(name) {
  const lower = String(name).toLowerCase();
  return lower === "江湖ol.cbe" || name === "江湖OL.cbe";
}

function firstExisting(paths) {
  for (const p of paths) {
    if (p && existsSync(p)) return p;
  }
  return null;
}

/** Slim CBE sidecar for extraResources (exe + dlls + 江湖OL.cbe + font). */
function stageCbeRuntime() {
  const dest = join(root, "build", "cbe-runtime");
  mkdirSync(join(dest, "CBE"), { recursive: true });
  const bin = join(repoRoot, "bin");
  const files = [
    ["main.exe", join(bin, "main.exe")],
    ["SDL2.dll", join(bin, "SDL2.dll")],
    ["unicorn.dll", join(bin, "unicorn.dll")],
    ["libwinpthread-1.dll", join(bin, "libwinpthread-1.dll")],
    [
      "font_gb.uc3",
      firstExisting([
        join(bin, "font_gb.uc3"),
        join(repoRoot, "JianghuOL", "app", "src", "main", "assets", "font_gb.uc3"),
      ]),
    ],
    [
      "servers.conf",
      firstExisting([
        join(bin, "servers.conf"),
        join(repoRoot, "JianghuOL", "app", "src", "main", "assets", "servers.conf"),
      ]),
    ],
    [
      "client_code_version.txt",
      firstExisting([
        join(root, "client_code_version.txt"),
        join(root, "public", "client_code_version.txt"),
      ]),
    ],
  ];
  for (const [name, src] of files) {
    if (!src || !existsSync(src)) {
      console.warn(`[build:exe] skip cbe sidecar ${name}`);
      continue;
    }
    copyFileSync(src, join(dest, name));
  }
  const cbeSrcDir = join(bin, "CBE");
  if (existsSync(cbeSrcDir)) {
    for (const name of readdirSync(cbeSrcDir)) {
      if (!isJianghuOlCbe(name)) continue;
      copyFileSync(join(cbeSrcDir, name), join(dest, "CBE", name));
    }
  }
  if (!existsSync(join(dest, "main.exe"))) {
    console.warn("[build:exe] bin/main.exe missing; PC 经典怀旧 will not start from this pack");
  } else {
    console.log(`[build:exe] staged CBE runtime → ${dest}`);
  }
}

if (!process.env.ELECTRON_MIRROR) {
  process.env.ELECTRON_MIRROR = "https://npmmirror.com/mirrors/electron/";
}
if (!process.env.ELECTRON_BUILDER_BINARIES_MIRROR) {
  process.env.ELECTRON_BUILDER_BINARIES_MIRROR =
    "https://npmmirror.com/mirrors/electron-builder-binaries/";
}

const staging = mkdtempSync(join(tmpdir(), "pixi-electron-"));
console.log(`[build:exe] staging → ${staging}`);
stageCbeRuntime();

const cli = join(root, "node_modules", "electron-builder", "cli.js");
const r = spawnSync(
  process.execPath,
  [cli, "--win", "portable", `--config.directories.output=${staging}`],
  {
    cwd: root,
    env: process.env,
    stdio: "inherit",
  },
);

if ((r.status ?? 1) !== 0) {
  console.error(`[build:exe] electron-builder failed (staging left at ${staging})`);
  process.exit(r.status ?? 1);
}

mkdirSync(outDir, { recursive: true });
for (const name of readdirSync(staging)) {
  const from = join(staging, name);
  let to = join(outDir, name);
  const st = statSync(from);
  if (existsSync(to)) {
    try {
      rmSync(to, { recursive: true, force: true });
    } catch (e) {
      if (st.isFile() && /\.exe$/i.test(name)) {
        const alt = name.replace(/(\.exe)$/i, "-new$1");
        to = join(outDir, alt);
        console.warn(
          `[build:exe] ${name} locked; writing ${alt} instead (${e instanceof Error ? e.message : e})`,
        );
      } else {
        throw e;
      }
    }
  }
  if (st.isDirectory()) {
    console.log(`[build:exe] copy dir ${name} → electron-release/`);
    cpSync(from, to, { recursive: true });
  } else {
    console.log(`[build:exe] copy ${basename(to)} → electron-release/`);
    copyFileSync(from, to);
  }
}

try {
  rmSync(staging, { recursive: true, force: true });
} catch {
  console.warn(`[build:exe] could not remove staging ${staging} (locked); safe to delete later`);
}

const portables = readdirSync(outDir).filter((n) =>
  /portable\.exe$/i.test(n),
);
console.log(
  `[build:exe] done → ${portables.map((n) => join(outDir, n)).join(", ") || outDir}`,
);
