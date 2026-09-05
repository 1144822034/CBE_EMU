/**
 * Sync JHOnlineData + font + client_code_version into PixiJS_web/public
 * for vite build / Capacitor offline packaging.
 *
 * Priority matches vite.config.ts: web/fs/JHOnlineData → bin/JHOnlineData.
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const rootDir = path.dirname(fileURLToPath(import.meta.url));
const pixiRoot = path.resolve(rootDir, "..");
const repoRoot = path.resolve(pixiRoot, "..");
const publicDir = path.join(pixiRoot, "public");

function resolveJhOnlineData() {
  const candidates = [
    path.join(repoRoot, "web", "fs", "JHOnlineData"),
    path.join(repoRoot, "bin", "JHOnlineData"),
  ];
  for (const c of candidates) {
    if (fs.existsSync(c)) return c;
  }
  return null;
}

function resolveFontFile(fileName, candidates) {
  for (const c of candidates) {
    if (fs.existsSync(c)) return c;
  }
  return null;
}

function resolveFont16Sky() {
  return resolveFontFile("font_16_sky.uc3", [
    path.join(repoRoot, "web", "fs", "font_16_sky.uc3"),
    path.join(repoRoot, "bin", "font_16_sky.uc3"),
  ]);
}

function resolveGb16Font() {
  return resolveFontFile("gb16.uc2.uc3", [
    path.join(repoRoot, "web", "fs", "gb16.uc2.uc3"),
    path.join(repoRoot, "bin", "gb16.uc2.uc3"),
    path.join(repoRoot, "bin", "multiplayer", "gb16.uc2.uc3"),
  ]);
}

function resolveFontGb() {
  return resolveFontFile("font_gb.uc3", [
    path.join(repoRoot, "bin", "font_gb.uc3"),
    path.join(repoRoot, "bin2", "font_gb.uc3"),
    path.join(repoRoot, "web", "fs", "font_gb.uc3"),
    path.join(
      repoRoot,
      "JianghuOL",
      "app",
      "src",
      "main",
      "assets",
      "font_gb.uc3",
    ),
  ]);
}

function resolveClientCodeVersion() {
  const candidates = [
    path.join(pixiRoot, "client_code_version.txt"),
    path.join(repoRoot, "client_code_version.txt"),
    path.join(repoRoot, "web", "fs", "JHOnlineData", "client_code_version.txt"),
    path.join(repoRoot, "bin", "JHOnlineData", "client_code_version.txt"),
    path.join(repoRoot, "bin", "client_code_version.txt"),
  ];
  for (const c of candidates) {
    if (fs.existsSync(c)) return c;
  }
  return null;
}

function rmrf(dir) {
  if (fs.existsSync(dir)) fs.rmSync(dir, { recursive: true, force: true });
}

function copyDir(src, dest) {
  fs.mkdirSync(dest, { recursive: true });
  for (const ent of fs.readdirSync(src, { withFileTypes: true })) {
    const from = path.join(src, ent.name);
    const to = path.join(dest, ent.name);
    if (ent.isDirectory()) copyDir(from, to);
    else fs.copyFileSync(from, to);
  }
}

const dataSrc = resolveJhOnlineData();
if (!dataSrc) {
  console.error(
    "[sync-offline-assets] missing JHOnlineData (tried web/fs and bin)",
  );
  process.exit(1);
}

fs.mkdirSync(publicDir, { recursive: true });
const dataDest = path.join(publicDir, "JHOnlineData");
console.log(`[sync-offline-assets] JHOnlineData ← ${dataSrc}`);
rmrf(dataDest);
copyDir(dataSrc, dataDest);

/* Pixi arena kill-streak VO: kept outside wiped JHOnlineData tree. */
const arenaSfxSrc = path.join(pixiRoot, "assets", "arena_kill_sfx");
const arenaSfxDest = path.join(dataDest, "arena_kill_sfx");
if (fs.existsSync(arenaSfxSrc)) {
  copyDir(arenaSfxSrc, arenaSfxDest);
  console.log(`[sync-offline-assets] arena_kill_sfx ← ${arenaSfxSrc}`);
} else {
  console.warn(
    "[sync-offline-assets] missing assets/arena_kill_sfx (kill-streak VO skipped)",
  );
}

/* Pixi map wings / mounts / animated titles — skip when PIXI_SKIP_COSMETIC_ASSETS=1
 * (无特效 APK). Sheets live in assets.bak_cosmetics/ when disabled. */
const skipCosmeticAssets = process.env.PIXI_SKIP_COSMETIC_ASSETS === "1";
for (const name of ["chibang", "zuoqi", "chenghao"]) {
  const src = path.join(pixiRoot, "assets", name);
  const dest = path.join(dataDest, name);
  if (skipCosmeticAssets) {
    if (fs.existsSync(dest)) {
      fs.rmSync(dest, { recursive: true, force: true });
      console.log(
        `[sync-offline-assets] removed ${name} (PIXI_SKIP_COSMETIC_ASSETS=1)`,
      );
    }
    continue;
  }
  if (fs.existsSync(src)) {
    copyDir(src, dest);
    console.log(`[sync-offline-assets] ${name} ← ${src}`);
  } else {
    /* Remove stale copies so APK / public do not keep old atlases. */
    if (fs.existsSync(dest)) {
      fs.rmSync(dest, { recursive: true, force: true });
      console.log(`[sync-offline-assets] removed stale ${name} (backed up under assets.bak_cosmetics)`);
    } else {
      console.warn(
        `[sync-offline-assets] missing assets/${name} (map ${name} skipped; see assets.bak_cosmetics)`,
      );
    }
  }
}

/* Pixi 水果机 UI（luckyFruit plist+png）。 */
const fruitUiSrc = path.join(pixiRoot, "assets", "fruit_ui");
const fruitUiDest = path.join(dataDest, "fruit_ui");
if (fs.existsSync(fruitUiSrc)) {
  copyDir(fruitUiSrc, fruitUiDest);
  console.log(`[sync-offline-assets] fruit_ui ← ${fruitUiSrc}`);
} else {
  console.warn(
    "[sync-offline-assets] missing assets/fruit_ui (fruit machine UI skipped)",
  );
}

const skySrc = resolveFont16Sky();
if (!skySrc) {
  console.error("[sync-offline-assets] missing font_16_sky.uc3");
  process.exit(1);
}
const skyDest = path.join(publicDir, "font_16_sky.uc3");
fs.copyFileSync(skySrc, skyDest);
console.log(`[sync-offline-assets] font_16_sky.uc3 ← ${skySrc}`);

const gb16Src = resolveGb16Font();
if (!gb16Src) {
  console.error("[sync-offline-assets] missing gb16.uc2.uc3");
  process.exit(1);
}
const gb16Dest = path.join(publicDir, "gb16.uc2.uc3");
fs.copyFileSync(gb16Src, gb16Dest);
console.log(`[sync-offline-assets] gb16.uc2.uc3 ← ${gb16Src}`);

const fontGbSrc = resolveFontGb();
if (!fontGbSrc) {
  console.error("[sync-offline-assets] missing font_gb.uc3");
  process.exit(1);
}
const fontDest = path.join(publicDir, "font_gb.uc3");
fs.copyFileSync(fontGbSrc, fontDest);
console.log(`[sync-offline-assets] font_gb.uc3 ← ${fontGbSrc}`);

const verSrc = resolveClientCodeVersion();
const verDest = path.join(publicDir, "client_code_version.txt");
let verNum = 0;
if (verSrc) {
  fs.copyFileSync(verSrc, verDest);
  console.log(`[sync-offline-assets] client_code_version.txt ← ${verSrc}`);
  verNum = Number.parseInt(fs.readFileSync(verDest, "utf8").trim(), 10) || 0;
} else {
  fs.writeFileSync(verDest, "5\n", "utf8");
  verNum = 5;
  console.warn(
    "[sync-offline-assets] no client_code_version.txt found; wrote default 5",
  );
}
const bundledPath = path.join(pixiRoot, "src", "net", "bundledCodeVersion.ts");
fs.writeFileSync(
  bundledPath,
  `/**\n` +
    ` * Bundled login codeVersion — kept in sync with \`client_code_version.txt\`\n` +
    ` * by \`scripts/sync-offline-assets.mjs\`. Used when runtime fetch misses\n` +
    ` * (Capacitor \`base: './'\`, offline, or absolute \`/…\` 404).\n` +
    ` */\n` +
    `export const BUNDLED_CLIENT_CODE_VERSION = ${verNum >>> 0};\n`,
  "utf8",
);
console.log(
  `[sync-offline-assets] bundledCodeVersion.ts ← ${verNum >>> 0}`,
);

console.log("[sync-offline-assets] done →", publicDir);
