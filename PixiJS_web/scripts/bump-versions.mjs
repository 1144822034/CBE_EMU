/**
 * Bump PixiJS client_code_version + Android versionCode / versionName.
 *
 * Usage (from repo or PixiJS_web):
 *   node scripts/bump-versions.mjs <clientCode> [versionCode] [versionName]
 *   bump-versions.bat 35
 *   bump-versions.bat 35 52
 *   bump-versions.bat 35 52 1.0.52
 *
 * If versionCode omitted: read gradle and +1.
 * If versionName omitted: 1.0.<versionCode>
 * server_code_version.txt is kept equal to client_code_version.
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const pixiRoot = path.resolve(__dirname, "..");
const repoRoot = path.resolve(pixiRoot, "..");
const gradlePath = path.join(pixiRoot, "android", "app", "build.gradle");

function die(msg) {
  console.error(`[bump-versions] ${msg}`);
  process.exit(1);
}

function readGradleVersions() {
  const text = fs.readFileSync(gradlePath, "utf8");
  const codeM = text.match(/\bversionCode\s+(\d+)/);
  const nameM = text.match(/\bversionName\s+"([^"]+)"/);
  if (!codeM) die(`versionCode not found in ${gradlePath}`);
  return {
    versionCode: Number.parseInt(codeM[1], 10),
    versionName: nameM ? nameM[1] : `1.0.${codeM[1]}`,
    text,
  };
}

function writeText(filePath, body) {
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
  fs.writeFileSync(filePath, body, "utf8");
  console.log(`[bump-versions] write ${path.relative(repoRoot, filePath)} ← ${JSON.stringify(body.trim())}`);
}

function writeVersionTxt(filePath, n) {
  writeText(filePath, `${n}\n`);
}

const args = process.argv.slice(2).filter((a) => a !== "--");
if (args.length < 1 || args.includes("-h") || args.includes("--help")) {
  console.log(`Usage:
  bump-versions.bat <client_code_version> [versionCode] [versionName]
  node scripts/bump-versions.mjs <client_code_version> [versionCode] [versionName]

Examples:
  bump-versions.bat 35           # client=35, versionCode=+1, versionName=1.0.<new>
  bump-versions.bat 35 52        # client=35, versionCode=52, versionName=1.0.52
  bump-versions.bat 35 52 1.0.52 # explicit all three`);
  process.exit(args.includes("-h") || args.includes("--help") ? 0 : 1);
}

const clientCode = Number.parseInt(args[0], 10);
if (!Number.isFinite(clientCode) || clientCode < 1) {
  die(`invalid client_code_version: ${args[0]}`);
}

const cur = readGradleVersions();
let versionCode =
  args[1] !== undefined ? Number.parseInt(args[1], 10) : cur.versionCode + 1;
if (!Number.isFinite(versionCode) || versionCode < 1) {
  die(`invalid versionCode: ${args[1]}`);
}

const versionName =
  args[2] !== undefined && String(args[2]).trim() !== ""
    ? String(args[2]).trim()
    : `1.0.${versionCode}`;

const codeTxtFiles = [
  path.join(pixiRoot, "client_code_version.txt"),
  path.join(pixiRoot, "public", "client_code_version.txt"),
  path.join(pixiRoot, "public", "JHOnlineData", "client_code_version.txt"),
  path.join(pixiRoot, "android", "app", "src", "main", "assets", "public", "client_code_version.txt"),
  path.join(pixiRoot, "android", "app", "src", "main", "assets", "public", "JHOnlineData", "client_code_version.txt"),
  path.join(repoRoot, "web", "fs", "JHOnlineData", "client_code_version.txt"),
  path.join(repoRoot, "bin", "JHOnlineData", "client_code_version.txt"),
  path.join(repoRoot, "JianghuOL", "app", "src", "main", "assets", "client_code_version.txt"),
];

const serverTxtFiles = [
  path.join(pixiRoot, "public", "JHOnlineData", "server_code_version.txt"),
  path.join(pixiRoot, "android", "app", "src", "main", "assets", "public", "JHOnlineData", "server_code_version.txt"),
  path.join(repoRoot, "web", "fs", "JHOnlineData", "server_code_version.txt"),
  path.join(repoRoot, "bin", "JHOnlineData", "server_code_version.txt"),
];

for (const f of codeTxtFiles) writeVersionTxt(f, clientCode);
for (const f of serverTxtFiles) writeVersionTxt(f, clientCode);

const bundledPath = path.join(pixiRoot, "src", "net", "bundledCodeVersion.ts");
writeText(
  bundledPath,
  `/**\n` +
    ` * Bundled login codeVersion — kept in sync with \`client_code_version.txt\`\n` +
    ` * by \`scripts/sync-offline-assets.mjs\`. Used when runtime fetch misses\n` +
    ` * (Capacitor \`base: './'\`, offline, or absolute \`/…\` 404).\n` +
    ` */\n` +
    `export const BUNDLED_CLIENT_CODE_VERSION = ${clientCode >>> 0};\n`,
);

let gradle = cur.text;
if (!/\bversionCode\s+\d+/.test(gradle)) die("versionCode pattern missing");
if (!/\bversionName\s+"[^"]+"/.test(gradle)) die("versionName pattern missing");
gradle = gradle.replace(/\bversionCode\s+\d+/, `versionCode ${versionCode}`);
gradle = gradle.replace(/\bversionName\s+"[^"]+"/, `versionName "${versionName}"`);
fs.writeFileSync(gradlePath, gradle, "utf8");
console.log(
  `[bump-versions] write ${path.relative(repoRoot, gradlePath)} ← versionCode ${versionCode}, versionName "${versionName}"`,
);

console.log(
  `[bump-versions] done: client_code_version=${clientCode}, versionCode=${versionCode}, versionName=${versionName} (was ${cur.versionCode}/${cur.versionName})`,
);
