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
const outDir = join(root, "electron-release");

if (!process.env.ELECTRON_MIRROR) {
  process.env.ELECTRON_MIRROR = "https://npmmirror.com/mirrors/electron/";
}
if (!process.env.ELECTRON_BUILDER_BINARIES_MIRROR) {
  process.env.ELECTRON_BUILDER_BINARIES_MIRROR =
    "https://npmmirror.com/mirrors/electron-builder-binaries/";
}

const staging = mkdtempSync(join(tmpdir(), "pixi-electron-"));
console.log(`[build:exe] staging → ${staging}`);

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
